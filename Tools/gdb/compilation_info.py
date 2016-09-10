#!/usr/bin/env python
import gdb
import re

INT = gdb.lookup_type("long int")

def get_ptr_val(ptr):
    return int(ptr.cast(INT))


class StringPrinter(object):
    def __init__(self, val):
        self.val = val

class CompilationInfoEntryPrinter(StringPrinter):
    def to_string(self):
        return "(\"%s\", 0x%x, %d)" % (self.val['name'].string(),
                                 get_ptr_val(self.val['start']),
                                 int(self.val['size']))

class CompilationInfoPrinter(StringPrinter):
    class Iterator(object):
        def __init__(self, array, size, last):
            self.array = array
            self.size = size
            self.last = last
            self.current = self.__next_index(self.last)

        def __next_index(self, index):
            if index >= self.size:
                return None
            if index == (self.size - 1):
                return 0
            return index + 1

        def __iter__(self):
            return self

        def __next__(self):
            if self.current is None:
                raise StopIteration
            val = self.array[self.current]
            if self.current == self.last:
                self.current = None
            else:
                self.current = self.__next_index(self.current)

            if get_ptr_val(val['start']) == 0:
                return self.__next__()

            return ("", val)

        def next(self):
            return self.__next__()


    def children(self):
        entries = self.val['entries']
        size = entries.type.sizeof / entries[0].type.sizeof
        return self.Iterator(entries, size, int(self.val['last']))
    
    def to_string(self):
        return "JSC::CompilationInfo"

    def display_hint(self):
        return 'array'


def add_pretty_printer():
    def lookup_function(val):
        
        lookup_tag = val.type.tag
        if lookup_tag == None:
            return None
        regex = re.compile("^JSC::CompilationInfoEntry$")
        if regex.match(lookup_tag):
            return CompilationInfoEntryPrinter(val)
        regex = re.compile("^JSC::CompilationInfo$")
        if regex.match(lookup_tag):
            return CompilationInfoPrinter(val)
        return None
    gdb.pretty_printers.append(lookup_function)

add_pretty_printer()
