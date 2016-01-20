// vim: noai:ts=4:sw=4:expandtab
#ifndef LIBGEODECOMP_PARALLELIZATION_AUTOTUNINGSIMULATOR_H
#define LIBGEODECOMP_PARALLELIZATION_AUTOTUNINGSIMULATOR_H

#include <libgeodecomp/misc/optimizer.h>
#include <libgeodecomp/misc/simulationfactory.h>
#include <libgeodecomp/misc/simulationparameters.h>
#include <libgeodecomp/io/initializer.h>
#include <libgeodecomp/io/varstepinitializerproxy.h>
#include <libgeodecomp/io/logger.h>
#include <cfloat>

#define LIBGEODECOMP_DEBUG_LEVEL 4
namespace LibGeoDecomp{

template<typename CELL_TYPE,typename OPTIMIZER_TYPE>
class AutoTuningSimulator
{
public:
    typedef boost::shared_ptr<SimulationFactory<CELL_TYPE> > SimFactoryPtr;
    class Simulation{
    public:
        Simulation(std::string name,
            SimFactoryPtr simFactory,
            SimulationParameters param,
            double fit = DBL_MAX):
            simulationType(name),
            simulationFactory(simFactory),
            parameters(param),
            fitness(fit)
        {}
        std::string simulationType;
        SimFactoryPtr simulationFactory;
        SimulationParameters parameters;
        double fitness;
    };

    typedef boost::shared_ptr<Simulation> SimulationPtr;

    AutoTuningSimulator(Initializer<CELL_TYPE> *initializer):
        optimizationSteps(10),
        varStepInitializer(initializer)
    {
        addNewSimulation("SerialSimulation",
            "SerialSimulation",
            varStepInitializer);

#ifdef LIBGEODECOMP_WITH_THREADS
        addNewSimulation("CacheBlockingSimulation",
            "CacheBlockingSimulation",
            varStepInitializer);
#endif

#ifdef __CUDACC__
#ifdef LIBGEODECOMP_WITH_CUDA
        addNewSimulation("CudaSimulation",
            "CudaSimulation",
            varStepInitializer);
#endif
#endif
    }

    ~AutoTuningSimulator()
    {}

    void addWriter(ParallelWriter<CELL_TYPE> *writer)
    {
        parallelWriters.push_back(boost::shared_ptr<ParallelWriter<CELL_TYPE> >(writer));
    }

    void addWriter(Writer<CELL_TYPE> *writer)
    {
        writers.push_back(boost::shared_ptr<Writer<CELL_TYPE> >(writer));
    }

    void addSteerer(const Steerer<CELL_TYPE> *steerer)
    {
        steerers.push_back(boost::shared_ptr<Steerer<CELL_TYPE> >(steerer));
    }

    void addNewSimulation(std::string name, std::string typeOfSimulation)
    {
        addNewSimulation(name, typeOfSimulation, varStepInitializer);
    }
private:
    void addNewSimulation(std::string name,
        std::string typeOfSimulation,
        VarStepInitializerProxy<CELL_TYPE> &initializer)
    {
        if (typeOfSimulation == "SerialSimulation") {
            SimFactoryPtr simFac_p(new SerialSimulationFactory<CELL_TYPE>(&initializer));
            SimulationPtr sim_p(new Simulation(
                    typeOfSimulation,
                    simFac_p,
                    simFac_p->parameters()));
            simulations[name] = sim_p;
            return;
        }

#ifdef LIBGEODECOMP_WITH_THREADS
        if (typeOfSimulation == "CacheBlockingSimulation") {
            SimFactoryPtr simFac_p(new CacheBlockingSimulationFactory<CELL_TYPE>(&initializer));
            SimulationPtr sim_p(new Simulation(
                    typeOfSimulation,
                    simFac_p,
                    simFac_p->parameters()));
            simulations[name] = sim_p;
            return;
        }
#endif

#ifdef __CUDACC__
#ifdef LIBGEODECOMP_WITH_CUDA
         if (typeOfSimulation == "CudaSimulation") {
            SimFactoryPtr simFac_p(new CudaSimulationFactory<CELL_TYPE>(&initializer));
            SimulationPtr sim_p(new Simulation(
                    typeOfSimulation,
                    simFac_p,
                    simFac_p->parameters()));
            simulations[name] = sim_p;
            return;
         }
#endif
#endif

        throw std::invalid_argument("SimulationFactory::addNewSimulation(): unknown simulator type");
    }
public:
    void deleteAllSimulations()
    {
        simulations.clear();
    }

    std::vector<std::string> getSimulationNames()
    {
        std::vector<std::string> result;
        typedef typename std::map<const std::string, SimulationPtr>::iterator IterType;
        for (IterType iter = simulations.begin(); iter != simulations.end(); iter++)
        {
            result.push_back(iter->first);
        }
        return result;
    }

    std::string getSimulatorType(std::string simulationName)
    {
        if (isInMap(simulationName)){
            return simulations[simulationName]->simulationType;
        } else {
            throw std::invalid_argument("getSimulatorType(simulationName)) get invalid simulationName");
        }
    }

    double getFitness(std::string simulationName)
    {
        if (isInMap(simulationName)){
            return simulations[simulationName]->fitness;
        } else {
            throw std::invalid_argument("getFitness(simulationName) get invalid simulationName");
        }
    }

    SimulationParameters getSimulationParameters(std::string simulationName)
    {
        if (isInMap(simulationName)){
            return simulations[simulationName]->parameters;
        } else {
            throw std::invalid_argument("getSimulationParameters(simulationName) get invalid simulationName");
        }
    }

    void setSimulationSteps(unsigned steps)
    {
        optimizationSteps = steps;
    }

    void setParameters(SimulationParameters params, std::string name)
    {
        if (isInMap(name)) {
            simulations[name]->parameters = params;
        } else {
            throw std::invalid_argument(
                "AutotuningSimulatro<...>::setParameters(params,name) get invalid name");
        }
    }

    void run()
    {
       runTest();
    }

    unsigned normalizeSteps(double goal, unsigned start = 5)
    {
        LOG(Logger::INFO, "normalizeSteps")
        if (! isInMap("SerialSimulation")){
            // FIXME Maybe an exzeption can be the heavy way!
            LOG(Logger::WARN, "AutoTuningSimulator::normalizeSteps(): no "
                        << "SerialSimulation available!")
            return 0;
        }
        if (start == 0) {
            LOG(Logger::WARN, "normalizeSteps is called with start = 0, "
                        << "this set start to default!")
            start = 5;
        }
        //TODO Ist es notwendig hier die Writer und steer schon hinzuzufügen?
        SimFactoryPtr factory = simulations["SerialSimulation"]->simulationFactory;
        unsigned steps = start;
        unsigned oldSteps = start;
        varStepInitializer.setMaxSteps(1);
        double variance = factory->operator()(simulations["SerialSimulation"]->parameters);
        double fitness = DBL_MAX;
        do {
            varStepInitializer.setMaxSteps(steps);
            fitness = factory->operator()(simulations["SerialSimulation"]->parameters);
            oldSteps = steps;
            LOG(Logger::DBG, "steps: " << steps)
            steps = ((double) steps / fitness) * (double)goal;
            if (steps < 1){
                steps =1;
            }
            LOG(Logger::DBG, "new calculated steps: " << steps)
            LOG(Logger::DBG, "fitness: " << fitness << " goal: " << goal)
        } while((!(fitness > goal + variance && fitness < goal - variance ))
             && (!(oldSteps <= 1 && fitness > goal)));

        return oldSteps;
    }

    void runTest()
    {
        typedef typename std::map<const std::string, SimulationPtr>::iterator IterType;

        for (IterType iter = simulations.begin(); iter != simulations.end(); iter++) {
            LOG(Logger::DBG, iter->first);
            for (unsigned i = 0; i < writers.size(); ++i) {
                iter->second->simulationFactory->addWriter(*writers[i].get()->clone());
            }
            for (unsigned i = 0; i < parallelWriters.size(); ++i) {
                iter->second->simulationFactory->addWriter(*parallelWriters[i].get()->clone());
            }
            for (unsigned i = 0; i < steerers.size(); ++i) {
                iter->second->simulationFactory->addSteerer(*steerers[i].get()->clone());
            }

            OPTIMIZER_TYPE optimizer(iter->second->parameters);
            iter->second->parameters = optimizer(
                optimizationSteps,
                *iter->second->simulationFactory);
            iter->second->fitness = optimizer.getFitness();

            LOG(Logger::DBG, "Result of the " << iter->second->simulationType
                << ": " << iter->second->fitness << std::endl
                << "new Parameters:"<< std::endl << iter->second->parameters
                << std::endl);
        }
    }

private:
    std::map<const std::string, SimulationPtr> simulations;
    unsigned optimizationSteps; // maximum number of Steps for the optimizer
    VarStepInitializerProxy<CELL_TYPE> varStepInitializer;
    std::vector<boost::shared_ptr<ParallelWriter<CELL_TYPE> > > parallelWriters;
    std::vector<boost::shared_ptr<Writer<CELL_TYPE> > > writers;
    std::vector<boost::shared_ptr<Steerer<CELL_TYPE> > > steerers;

    bool isInMap(const std::string name)const
    {
        if (simulations.find(name) == simulations.end()){
            return false;
        } else {
            return true;
        }
    }
}; //AutoTunigSimulator
} // namespace LibGeoDecomp

#endif
