#include <co/routine_specific_ptr.hpp>

#include <catch.hpp>

struct TestObject
{
   int* mDestructorCalls;
   
   TestObject(int& destructorCalls)
    : mDestructorCalls(&destructorCalls)
   {
   }
   
   TestObject(const TestObject&) = delete;
   TestObject& operator=(const TestObject&) = delete;
   
   ~TestObject()
   {
      (*mDestructorCalls)++;
   }
};

SCENARIO("use of co::Routine::SpecificPtr")
{
   GIVEN("a default constructed co::Routine::SpecificPtr")
   {
       co::Routine::SpecificPtr<TestObject> ptr;
       
       WHEN("get() is call from an ordinary thread")
       {
          auto val = ptr.get();
          
          THEN("the result should be nullptr")
          {
            REQUIRE(val == nullptr);
          }
       }
       
       WHEN("get() is call from an coroutine")
       {
          TestObject* val = nullptr;
          co::Routine([&](){ val = ptr.get(); }).join();
          
          THEN("the result should be nullptr")
          {
            REQUIRE(val == nullptr);
          }
       }
       
       WHEN("the pointer is reset() to an object from an ordinary thread")
       {
          int destructorCalls{};
          auto testObjMainThread = new TestObject{destructorCalls}; 
          ptr.reset(testObjMainThread);
          
          THEN("get() should return the raw pointer to this object when called from the same thread")
          {
            REQUIRE(ptr.get() == testObjMainThread);
          }
          
          THEN("get() should still return nullptr when called from an other thread")
          {
             std::thread{[&](){REQUIRE(ptr.get() == nullptr);}}.join();             
          }
          
          THEN("get() should still return nullptr when called from an coroutine")
          {
             co::Routine{[&](){REQUIRE(ptr.get() == nullptr);}}.join();             
          }
          
          AND_WHEN("the pointer is reset to nullptr")
          {
             ptr.reset();
             
             THEN("the objects destructor should have been called")
             {
                REQUIRE(destructorCalls == 1);
             }
             
             THEN("get() should return nullptr again on the initial thread")
             {
                REQUIRE(ptr.get() == nullptr);
             }
          }
       }
      
       WHEN("the pointer is reset() to an object from an coroutine")
       {
          int destructorCalls{};
          
          co::Routine{[&](){
            auto testObjMainThread = new TestObject{destructorCalls}; 
            ptr.reset(testObjMainThread);
            
            THEN("get() should return the raw pointer to this object when called from the same thread")
            {
               REQUIRE(ptr.get() == testObjMainThread);
            }
            
            THEN("get() should still return nullptr when called from an other thread")
            {
               std::thread{[&](){REQUIRE(ptr.get() == nullptr);}}.join();             
            }
            
            THEN("get() should still return nullptr when called from an other coroutine")
            {
               co::Routine{[&](){REQUIRE(ptr.get() == nullptr);}}.join();             
            }
          
            AND_WHEN("the pointer is reset to nullptr")
            {
               ptr.reset();
               
               THEN("the objects destructor should have been called")
               {
                  REQUIRE(destructorCalls == 1);
               }
               
               THEN("get() should return nullptr again on the coroutine")
               {
                  REQUIRE(ptr.get() == nullptr);
               }
            }             
         }}.join();
                        
         AND_WHEN("the coroutine is left")
         {
            ptr.reset();
            
            THEN("the objects destructor should have been called")
            {
               REQUIRE(destructorCalls == 1);
            }
            
            THEN("get() should return nullptr again on the initial thread")
            {
               REQUIRE(ptr.get() == nullptr);
            }
         }             

       }
   }   
}
