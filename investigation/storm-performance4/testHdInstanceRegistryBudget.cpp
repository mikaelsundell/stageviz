// Licensed under the terms of the OpenUSD LICENSE.txt file.
#include "pxr/imaging/hd/instanceRegistry.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
PXR_NAMESPACE_USING_DIRECTIVE
namespace {
std::atomic<int> destroyed{0};
struct Value {
    explicit Value(bool slow=false):slow(slow) {}
    ~Value() { if(slow) std::this_thread::sleep_for(std::chrono::milliseconds(2)); ++destroyed; }
    bool slow;
};
using Registry=HdInstanceRegistry<std::shared_ptr<Value>>;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void insert(Registry& registry,uint64_t key,bool slow=false) {
    auto item=registry.GetInstance(key);
    if(item.IsFirstInstance()) item.SetValue(std::make_shared<Value>(slow));
}
}
int main() {
    try {
        {
            Registry registry;Registry::GarbageCollectionCursor cursor;
            for(int i=0;i<100;++i) insert(registry,i);
            const auto first=registry.GarbageCollectStep(&cursor,7,std::chrono::microseconds(0));
            require(first.visited==7 && first.reclaimed==7 && first.pending,"visit budget limits a step");
            size_t visited=first.visited,reclaimed=first.reclaimed;
            while(cursor.active) {
                auto step=registry.GarbageCollectStep(&cursor,7,std::chrono::microseconds(0));
                require(step.visited<=7,"every step respects visit cap");
                visited+=step.visited;reclaimed+=step.reclaimed;
            }
            require(visited==100 && reclaimed==100 && registry.size()==0,"complete without skipped nodes");
        }
        {
            Registry registry;Registry::GarbageCollectionCursor cursor;
            std::vector<std::shared_ptr<Value>> held;
            for(int i=0;i<50;++i) {
                auto item=registry.GetInstance(i);item.SetValue(std::make_shared<Value>());held.push_back(item.GetValue());
            }
            auto first=registry.GarbageCollectStep(&cursor,30,std::chrono::microseconds(0));
            require(first.reclaimed==0 && registry.size()==50,"live values remain accessible");
            held.clear();
            // Force rehash and introduce nodes that may sort before the saved
            // cursor. A requested rescan must revisit those and earlier nodes.
            for(int i=50;i<2000;++i) insert(registry,i);
            cursor.RequestRescan();
            for(int steps=0;cursor.active && steps<1000;++steps)
                registry.GarbageCollectStep(&cursor,17,std::chrono::microseconds(0));
            require(!cursor.active && registry.size()==0,"rescan handles released and newly inserted values");
        }
        {
            Registry registry;Registry::GarbageCollectionCursor cursor;
            for(int i=0;i<10;++i) insert(registry,i,true);
            auto step=registry.GarbageCollectStep(&cursor,0,std::chrono::microseconds(500));
            require(step.reclaimed==1 && step.pending,"soft time budget stops after one costly destructor");
            require(step.destroyNs>=1000000,"destruction timer includes the costly destructor");
            registry.GarbageCollectStep(&cursor,0,std::chrono::microseconds(0));
            require(!cursor.active && registry.size()==0,"unlimited drain completes pending work");
        }
        {
            Registry registry;Registry::GarbageCollectionCursor cursor;
            for(int i=0;i<100;++i) insert(registry,i);
            registry.GarbageCollectStep(&cursor,2,std::chrono::microseconds(0));
            std::shared_ptr<Value> reused;
            const auto key=registry.begin()->first;
            { auto item=registry.GetInstance(key);reused=item.GetValue(); }
            while(cursor.active) registry.GarbageCollectStep(&cursor,9,std::chrono::microseconds(0));
            require(registry.size()==1,"a value reacquired between frames remains live");
            { auto item=registry.GetInstance(key);require(item.GetValue()==reused && !item.IsFirstInstance(),"reuse preserves the cached object"); }
            reused.reset();
            registry.GarbageCollectStep(&cursor,0,std::chrono::microseconds(0));
            require(registry.size()==0,"reused value is reclaimed after its last client releases it");
        }
        {
            Registry registry;Registry::GarbageCollectionCursor cursor;
            auto step=registry.GarbageCollectStep(&cursor,1,std::chrono::microseconds(1));
            require(!step.pending && !step.visited,"empty registries settle immediately");
        }
        require(destroyed==2210,"every unused value destroyed exactly once");
        std::cout<<"PASS: visit/time budgets, live references, rehash/rescan, empty and final drain\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
