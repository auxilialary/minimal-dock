#include "../src/shell/Autostart.h"
#include <cassert>
#include "../src/app/StartupPolicy.h"
#include <iostream>
struct Fake : Autostart::Store {
 std::wstring value; bool exists = false; LSTATUS failure = 0; int writes = 0;
 LSTATUS Read(std::wstring& out) override { out=value; return failure ? failure : exists ? 0 : ERROR_FILE_NOT_FOUND; }
 LSTATUS Write(const std::wstring& in) override { ++writes; if(failure) return failure; value=in; exists=true; return 0; }
 LSTATUS Remove() override { if(failure) return failure; exists=false; return 0; }
};
int main() {
 assert(!StartupPolicy::CanProtect(true,true,true,false,true));
 assert(!StartupPolicy::CanProtect(true,true,true,true,false));
 assert(StartupPolicy::CanProtect(true,true,true,true,true));
 assert(!StartupPolicy::CanProtect(false,true,true,true,true));
 assert(!StartupPolicy::CanProtect(true,false,true,true,true));
 assert(!StartupPolicy::CanProtect(true,true,false,true,true));
 assert(StartupPolicy::RetryDelay(1)==250 && StartupPolicy::RetryDelay(4)==2000);
 assert(StartupPolicy::RetryDelay(5)==0);
 Fake store;
 const std::wstring path=L"C:\\Fixtures\\\u0422\u0435\u0441\u0442\\Dock Space\\Dock.exe";
 assert(!Autostart::Inspect(store,path).enabled && store.writes==0);
 assert(Autostart::Set(store,path,true)==0);
 assert(store.value==L"\"C:\\Fixtures\\\u0422\u0435\u0441\u0442\\Dock Space\\Dock.exe\"");
 assert(Autostart::Inspect(store,path).enabled);
 assert(Autostart::Inspect(store,L"C:\\Moved\\Dock.exe").outdated);
 assert(Autostart::Set(store,L"C:\\Moved\\Dock.exe",true)==0);
 assert(Autostart::Set(store,path,false)==0 && !store.exists);
 store.failure=ERROR_ACCESS_DENIED;
 assert(Autostart::Set(store,path,true)==ERROR_ACCESS_DENIED);
 assert(Autostart::Inspect(store,path).error==ERROR_ACCESS_DENIED);
 assert(Autostart::Set(store,L"",true)==ERROR_BAD_PATHNAME);
 std::cout << "Autostart fake-store tests passed; no registry writes.\n";
}
