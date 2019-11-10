#pragma once
#include <Utils/IModule.hpp>
#include <Utils/Hook.hpp>

extern "C"
{
    #include <netinet/in.h>
    #include <machine/reg.h>
};

struct trapframe;

namespace Mira
{
    namespace Plugins
    {
        class Debugger : public Mira::Utils::IModule
        {
        private:
            enum 
            { 
                // Configuration
                Debugger_MaxTaintedThreads = 20,
                Debugger_MaxPath = 260
            };
            
            // Remote GDB
            int32_t m_GdbSocket;
            struct sockaddr_in m_GdbAddress;

            // Debugger
            int32_t m_ProcessId;
            struct reg m_Registers;
            struct fpreg m_FloatingRegisters;
            struct dbreg m_DebugRegisters;
            Utils::Hook* m_TrapFatalHook;


        protected:
            static void OnTrapFatal(struct trapframe* p_Frame, vm_offset_t p_Eva);
            static bool IsStackSpace(void* p_Address);

        public:
            Debugger();
            virtual ~Debugger();

            virtual const char* GetName() override { return "Debugger"; }
            virtual const char* GetDescription() override { return "GDB compatible debugger with ReClass abilties."; }
            virtual bool OnLoad() override;
            virtual bool OnUnload() override;

            virtual bool OnSuspend() override;
            virtual bool OnResume() override;

        protected:
            uint8_t StubGetChar(void);
            bool StubPutChar(uint8_t p_Char);
            int32_t StubReadByte(void* p_Address, char* p_OutValue);
            int32_t StubWriteByte(void* p_Address, char p_Value);
            int32_t StubContinue();
            int32_t StubStep();

            bool LaunchApplication(const char* p_Path);

        private:
            bool StartStubServer();
            bool TeardownStubServer();

            bool Attach(int32_t p_ProcessId, bool p_StopOnAttach = false);
            bool Detach(bool p_ResumeOnDetach = false);

            uint32_t ReadData(uint64_t p_Address, uint8_t* p_OutData, uint32_t p_Size);
            uint32_t WriteData(uint64_t p_Address, uint8_t* p_Data, uint32_t p_Size);

            bool SingleStep();

            bool UpdateRegisters();
            bool UpdateWatches();
            bool UpdateBreakpoints();

            bool UpdateAll();
        };
    }
}