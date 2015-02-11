/* This class will declare a method inside itself, but will have a
   method declared outside it as well */
class Foo {
  proc internalMeth() {

  }
}

// We expect these two methods to be printed outside of the class indentation
// level
proc Foo.externalMeth1() {

}

/* This method has a comment attached to it */
proc Foo.externalMeth2() {

}
