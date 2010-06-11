// Copyright 2009-2010 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2010, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef SST_EVENT_H
#define SST_EVENT_H

#include <sst/core/eventFunctor.h>
#include <sst/core/activity.h>
// #include <sst/core/link.h>


namespace SST {

class Link;
    
// #include <sst/core/sst.h>


typedef union {
    Link* ptr;
    LinkId_t id;
} LinkUnion;

    
class NewEvent : public Activity {

public:
    NewEvent() : Activity() {}
    ~NewEvent() {}

    void execute(void);
    
private:
    LinkUnion link;
    
};



class Event : public Activity {
public:
/*     typedef EventHandlerBase<bool,Event*> Handler_t; */

    Event() : Activity() {}
    virtual ~Event() = 0;

    void execute(void) {
// 	delivery_link->deliverEvent(this);
    }
    
private:

    Link* delivery_link;
    LinkId_t link_id;

    friend class boost::serialization::access;
    template<class Archive>
    void
    serialize(Archive & ar, const unsigned int version )
    {
    }
};

}

#endif // SST_EVENT_H
