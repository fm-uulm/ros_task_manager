#ifndef TASK_DEFINITION_H
#define TASK_DEFINITION_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ros/ros.h>
#include <boost/enable_shared_from_this.hpp>

#include <string>
#include <task_manager_msgs/TaskStatus.h>
#include <task_manager_msgs/TaskDescription.h>
#include <dynamic_reconfigure/ConfigDescription.h>
#include <dynamic_reconfigure/server.h>
#include <dynamic_reconfigure/Config.h>
#include <dynamic_reconfigure/config_tools.h>

namespace task_manager_lib {
    class DynamicTask;

    // Enum defined in TaskStatus msg
    typedef unsigned int TaskIndicator;

    // Extension of the dynamic_reconfigure::Config class to add more 
    // object like functions and conversion
    class TaskParameters: public dynamic_reconfigure::Config {
        protected:
            // Copy-pasted from dynamic_reconfigure::ConfigTools::getParameter
            template <class VT, class T>
                bool setParameter(std::vector<VT> &vec, const std::string &name, const T &val)
                {
                    for (typename std::vector<VT>::iterator i = vec.begin(); i != vec.end(); i++)
                        if (i->name == name)
                        {
                            i->value = val;
                            return true;
                        }
                    return false;
                }


        public:
            TaskParameters() 
                : dynamic_reconfigure::Config() { setDefaultParameters(); }
            TaskParameters(const dynamic_reconfigure::Config & cfg) 
                : dynamic_reconfigure::Config(cfg) {}
            TaskParameters(const TaskParameters & cfg) 
                : dynamic_reconfigure::Config(cfg) {}

            // The default values that all tasks will be expected. In most cases,
            // this is actually overwritten by the values in in the .cfg file if
            // one is used
            void setDefaultParameters() {
                setParameter("task_rename",std::string());
                setParameter("main_task",true);
                setParameter("task_period",1.);
                setParameter("task_timeout",-1.);
            }

            // Convert the parameter to a XXXConfig class, as generated by an .cfg
            // file. See dynamic_reconfigure/templates/TypeConfig.h
            template <class CFG>
                CFG toConfig() const {
                    CFG cfg = CFG::__getDefault__();
                    dynamic_reconfigure::Config drc = *this;
                    cfg.__fromMessage__(drc);
                    return cfg;
                }

            // Convert the data from a XXXConfig class, to a parameter structure
            // suitable for sending as a ROS message. A bit too much copies in this
            // functions.
            template <class CFG>
                void fromConfig(const CFG & cfg) {
                    dynamic_reconfigure::Config drc = *this;
                    cfg.__toMessage__(drc);
                    *this = drc;
                }

            // Read a parameter from the structure, and return false if it is
            // missing. Mostly a convenience function to avoid the long namespaces.
            template <class T>
                bool getParameter(const std::string &name, T &val) const
                {
                    return dynamic_reconfigure::ConfigTools::getParameter(
                            dynamic_reconfigure::ConfigTools::getVectorForType(*this, val), name, val);
                }

            // Set a parameter int the structure. This is missing in the
            // ConfigTools class.
            template <class T>
                void setParameter(const std::string &name, const T &val)
                {
                    if (!setParameter(dynamic_reconfigure::ConfigTools::getVectorForType(*this, val), name, val)) {
                        dynamic_reconfigure::ConfigTools::appendParameter(*this,name,val);
                    }
                }

            // Merge a set of parameters with the current one. Any parameter in
            // tnew that is already in 'this' is updated. If it is not there yet,
            // it is inserted.
            void update(const TaskParameters & tnew) {
                for (unsigned int i = 0; i < tnew.bools.size(); i++) {
                    setParameter(tnew.bools[i].name,(bool)tnew.bools[i].value);
                }
                for (unsigned int i = 0; i < tnew.ints.size(); i++) {
                    setParameter(tnew.ints[i].name,(int)tnew.ints[i].value);
                }
                for (unsigned int i = 0; i < tnew.doubles.size(); i++) {
                    setParameter(tnew.doubles[i].name,(double)tnew.doubles[i].value);
                }
                for (unsigned int i = 0; i < tnew.strs.size(); i++) {
                    setParameter(tnew.strs[i].name,tnew.strs[i].value);
                }
            }

            // Dump the content of the parameters. Mostly for debug
            void print(FILE * fp=stdout) const {
                for (unsigned int i = 0; i < bools.size(); i++) {
                    fprintf(fp,"bool: %s = %s\n",bools[i].name.c_str(),bools[i].value?"true":"false");
                }
                for (unsigned int i = 0; i < ints.size(); i++) {
                    fprintf(fp,"int : %s = %d\n",ints[i].name.c_str(),ints[i].value);
                }
                for (unsigned int i = 0; i < doubles.size(); i++) {
                    fprintf(fp,"dbl : %s = %e\n",doubles[i].name.c_str(),doubles[i].value);
                }
                for (unsigned int i = 0; i < strs.size(); i++) {
                    fprintf(fp,"str : %s = '%s'\n",strs[i].name.c_str(),strs[i].value.c_str());
                }
            }
    };

    /**
     * Basic function to build the string representation of one of the status above
     * */
    extern
        const char * taskStatusToString(TaskIndicator ts);

    /**
     *
     * Mother class of all tasks. Contains all the generic tools to define a task.
     * Must be inherited. Such a task can be periodic or aperiodic
     * 
     * */
    class TaskDefinition: public boost::enable_shared_from_this<TaskDefinition>
    {
        public:
            /**
             * Local exception type for invalid paramters in task argument.
             * Can be thrown by Configure and Initialise member functions
             * */
            struct InvalidParameter : public std::exception {
                std::string text;
                InvalidParameter(const std::string & txt) : text("Invalid Parameter: ") {
                    text += txt;
                }
                virtual ~InvalidParameter() throw () {}
                virtual const char * what() const throw () {
                    return text.c_str();
                }
            };

        protected:
            /**
             * Task name, for display and also used to find the task in the
             * scheduler 
             * */
            std::string name;
            /**
             * Help string that the task can return on request. For display only.
             * */
            std::string help;
            /**
             * Flag indicating if the task is periodic (iterate is called at regular
             * frequency) or aperiodic (iterate is called only once, but a
             * monitoring function reports regularly about the task state).
             * */
            bool periodic;

            /**
             * Storage for some task status string, if required. Can only be set
             * with setStatusString
             * */
            std::string statusString;

            /**
             * Storage for the current status of the task. Automatically updated
             * from the output of the configure, initialise, iterate and terminate
             * functions
             * */
            TaskIndicator taskStatus;

            /**
             * Timeout value for this cycle
             * */
            double timeout;


            /**
             * value of the parameters
             * */
            TaskParameters config;

            /**
             * Id of the task, set by the scheduler in the list of known task.
             * Should be unique for a given name
             * */
            unsigned int taskId;
            /**
             * Id of the task, set by the scheduler when initializing the
             * instance
             * */
            unsigned int runId;
        public:
            // All the class below are intended for generic use

            // Default constructor:
            // tname: the task name
            // thelp: the task description
            // isperiodic: tells if the class will be executed recurringly
            // (isperiodic = true), or if the class will be executed in its own
            // thread and will report its status on its own. 
            // deftTimeout: default task timeout, typically overridden by the
            // task parameters.
            TaskDefinition(const std::string & tname, const std::string & thelp, 
                    bool isperiodic, double deftTimeout) :
                name(tname), help(thelp), periodic(isperiodic), 
                taskStatus(task_manager_msgs::TaskStatus::TASK_NEWBORN), 
                timeout(deftTimeout), taskId(-1), runId(-1) {}
            virtual ~TaskDefinition() {
                // printf("Delete task '%s'\n",name.c_str());
                // fflush(stdout);
            }


            // Set the task runtime id . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual unsigned int getRuntimeId() const;
            // Set the task runtime id . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual unsigned int getTaskId() const;

            // Set the task name . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual void setName(const std::string & n);
            // Get the task name . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual const std::string & getName() const;
            // Get the task description . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual const std::string & getHelp() const;

            // Report if the task is meant to be periodic. See above for details
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual bool isPeriodic() const;

            // Report the task timeout
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual double getTimeout() const;

            // Reset the status indicator and empty the status string
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual void resetStatus();

            // Get the status indicator 
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual TaskIndicator getStatus() const;
            // Update the task status
            virtual void setStatus(const TaskIndicator & ti);
            // Get the status string 
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual const std::string & getStatusString() const;
            // Update the task status string
            virtual void setStatusString(const std::string & s); 


            // Get the task parameters, in case one does not store them at
            // initialisation
            // Has to be virtual because it is overloaded by the dynamic class proxy.
            virtual const TaskParameters & getConfig() const;

            // Provide an instance of the class (or a derivative of it), with
            // its own internal variables that can be run multiple time. 
            // Careful: getInstance must preserve taskId
            // Default implementation could be:
            // {
            //     return shared_from_this();
            // }
            virtual boost::shared_ptr<TaskDefinition> getInstance() = 0; 

        public:
            // All the functions below are intended for the TaskScheduler.
            // Set the task runtime id . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual void setRuntimeId(unsigned int id);
            // Set the task runtime id . Has to be virtual because it is overloaded by
            // the dynamic class proxy.
            virtual void setTaskId(unsigned int id);

            // Test if a class is an instance of def
            bool isAnInstanceOf(const TaskDefinition & def);
            bool isAnInstanceOf(const boost::shared_ptr<TaskDefinition> & def);

            // Call the virtual configure function, but prepare the class before
            // hand.
            void doConfigure(unsigned int taskId, const TaskParameters & parameters);

            // Call the virtual initialise function, but prepare the class before
            // hand.
            void doInitialise(unsigned int runtimeId, const TaskParameters & parameters);

            // Call the virtual iterate function, but prepare the class before
            // hand.
            void doIterate();

            // Call the virtual terminate function, but prepare the class before
            // hand.
            void doTerminate();

            // Output a debut string, prefixed by the task name
            void debug(const char *stemplate,...) const; 

            // Get the task description as a combination of task-specific
            // information and dynamic_reconfigure::ConfigDescription (assuming the
            // task as a config file)
            task_manager_msgs::TaskDescription getDescription() const;

            // Get the status as a message ready to be published over ROS
            task_manager_msgs::TaskStatus getRosStatus() const;

        public:
            // The functions below are virtual pure and must be implemented by the
            // specific task by linking in the type generated from the .cfg file. 
            // See the TaskDefinitionWithConfig class for details.

            // Return the parameters as read from the parameter server. Returns the
            // default parameters otherwise.
            virtual TaskParameters getParametersFromServer(const ros::NodeHandle &nh) = 0;

            // Return the default parameters, typically from the default value
            // defined in the .cfg file.
            virtual TaskParameters getDefaultParameters() const = 0;

            /**
             * description of the parameters
             * */
            virtual dynamic_reconfigure::ConfigDescription getParameterDescription() const = 0;

        protected:
            // Set of functions only useful for derived classes
            friend class DynamicTask;

            // Update the description string
            void setHelp(const std::string & h) {help = h;}
            // Update the periodic flag. Do not do that after starting the task.
            void setPeriodic(bool p) {periodic = p;}
            // Update the task timeout
            void setTimeout(double tout) {timeout = tout;}
        protected:
            // Set of functions that must be implemented by any inheriting class

            // Configure is called only once when the task scheduler is started.
            virtual TaskIndicator configure(const TaskParameters & parameters) throw (InvalidParameter) = 0;

            // Initialise is called once every time the task is launched
            virtual TaskIndicator initialise(const TaskParameters & parameters) throw (InvalidParameter) = 0;

            // iterate is called only once for non periodic tasks. It is called
            // iteratively with period 'task_period' for periodic class. 
            virtual TaskIndicator iterate() = 0;

            // Terminate is called once when the task is completed, cancelled or
            // interrupted.
            virtual TaskIndicator terminate() = 0;

    };

    // Templated class specialising some of the virtual functions of a
    // TaskDefinition based on the data available in a XXXConfig class generated
    // from a .cfg file. This is still a virtual pure class.
    template <class CFG, class Instance>
        class TaskDefinitionWithConfig : public TaskDefinition {
            protected:
                typedef TaskDefinitionWithConfig<CFG,Instance> Parent;

                class DynRecfgData {
                    protected:
                        boost::shared_ptr< dynamic_reconfigure::Server<CFG> > srv;
                        boost::recursive_mutex mutex;
                        typename dynamic_reconfigure::Server<CFG>::CallbackType f;               
                    public:
                        DynRecfgData(TaskDefinitionWithConfig * td, const CFG & cfg) {
                            char node_name[td->getName().size()+64];
                            sprintf(node_name,"~%s_%d",td->getName().c_str(),td->getRuntimeId());
                            f = boost::bind(&Parent::reconfigureCallback,td, _1, _2);
                            srv.reset(new dynamic_reconfigure::Server<CFG>(mutex,ros::NodeHandle(node_name)));
                            srv->updateConfig(cfg);
                            srv->setCallback(f);
                        }
                };
                friend class DynRecfgData;
                boost::shared_ptr<DynRecfgData> recfg;

                CFG cfg;

                // Callback function to be bound to the server. It just calls
                // the virtualised function in case it has been overloaded.
                void reconfigureCallback(CFG &config, uint32_t level) {
                    this->reconfigure(config, level);
                }


                // Setup a dynamic reconfigure server that just update all the
                // config. To be updated 
                virtual void reconfigure(CFG &config, uint32_t level) {
                    cfg = config;
                }
            public:
                // Same constructor as the normal TaskDefinition
                TaskDefinitionWithConfig(const std::string & tname, const std::string & thelp, 
                        bool isperiodic, double deftTimeout) : TaskDefinition(tname,thelp,isperiodic,deftTimeout) {
                }
                virtual ~TaskDefinitionWithConfig() {}

                // Returns the parameter description from the Config class.
                virtual dynamic_reconfigure::ConfigDescription getParameterDescription() const {
                    return CFG::__getDescriptionMessage__();
                }

                // Return the default parameters from the Config class.
                virtual TaskParameters getDefaultParameters() const {
                    TaskParameters tp;
                    CFG c = CFG::__getDefault__();
                    c.__toMessage__(tp);
                    return tp;
                }


                // Read the parameter from the parameter server using the Config class.
                virtual TaskParameters getParametersFromServer(const ros::NodeHandle & nh) {
                    TaskParameters tp;
                    CFG c = CFG::__getDefault__();
                    c.__fromServer__(nh);
                    c.__toMessage__(tp);
                    return tp;
                }

                virtual TaskIndicator configure(const TaskParameters & parameters) throw (InvalidParameter)
                {
                    return task_manager_msgs::TaskStatus::TASK_CONFIGURED;
                }

                virtual TaskIndicator initialise(const TaskParameters & parameters) throw (InvalidParameter)
                {
                    cfg = parameters.toConfig<CFG>();
                    recfg.reset(new DynRecfgData(this, cfg));
                    return task_manager_msgs::TaskStatus::TASK_INITIALISED;
                }

                virtual TaskIndicator terminate()
                {
                    return task_manager_msgs::TaskStatus::TASK_TERMINATED;
                }

                virtual boost::shared_ptr<TaskDefinition> getInstance() {
                    return boost::shared_ptr<TaskDefinition>(new Instance(*(Instance*)this));
                }
        };

    /**
     * Empty class, to be inherited for a specific application. The existence of
     * the class provides an easy way to use the dynamic_cast to check the type of
     * the argument.\
     * */
    class TaskEnvironment {
        public:
            TaskEnvironment() {}
            virtual ~TaskEnvironment() {}
    };

    // Function type for the TaskFactoryObject function that will be inserted into
    // each class to be used as a dynamic class (i.e. a .so library).
    //
    // Note that a class inherited from TaskDefinition and meant to be used with
    // the dynamic loading mecanism must have a constructor with the following
    // profile:
    //  TaskXXX(boost::shared_ptr<TaskEnvironment> env)
    //
    typedef boost::shared_ptr<TaskDefinition> (*TaskFactory)(boost::shared_ptr<TaskEnvironment>&);
#define DYNAMIC_TASK(T) extern "C" {\
    boost::shared_ptr<task_manager_lib::TaskDefinition> TaskFactoryObject(boost::shared_ptr<task_manager_lib::TaskEnvironment> &environment) {\
        return boost::shared_ptr<task_manager_lib::TaskDefinition>(new T(environment));\
    } \
}

    };


#endif // TASK_DEFINITION_H
