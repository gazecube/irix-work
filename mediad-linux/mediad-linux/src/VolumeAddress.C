#include "VolumeAddress.H"

#include <assert.h>
#include <irixdev.h>

#include <stdio.h>

#include <sys/dir.h>

#include "DeviceAddress.H"
#include "PartitionAddress.H"
#include "operators.H"

VolumeAddress::VolumeAddress(const VolumeAddress& that)
: _refcount(0)
{
    if (that._refcount == 0)
    {
	//  That is a VolumeAddress, not a derived class.
	//  Share its rep.

	_rep = that._rep;
	if (_rep)
	    _rep->_refcount++;
    }
    else
    {
	//  That is a derived class.  Clone it.
	
	_rep = that.clone();
    }	
}

VolumeAddress&
VolumeAddress::operator = (const VolumeAddress& that)
{
    if (this != &that)
    {   if (_rep && !--_rep->_refcount)
	    delete _rep;
	_rep = that._rep;
	if (_rep)
	    _rep->_refcount++;
    }
    return *this;
}

VolumeAddress::~VolumeAddress()
{
    assert(!_rep || _rep->_refcount > 0);
    if (_rep && !--_rep->_refcount)
	delete _rep;
}

VolumeAddress *
VolumeAddress::clone() const
{
    return new VolumeAddress(*this);
}

const PartitionAddress *
VolumeAddress::partition(unsigned index) const
{
    if (_rep)
	return _rep->partition(index);
    else
	return NULL;
}

bool
VolumeAddress::operator == (const VolumeAddress& that) const
{
    if (this == &that)
	return true;
    if (_rep)
	return *_rep == that;
    if (that._rep)
	return *this == *that._rep;
    for (int i = 0; ; i++)
    {   const PartitionAddress *a = partition(i);
	const PartitionAddress *b = that.partition(i);
	if (!a || !b)
	    return !a && !b;
	if (a->partition() != b->partition())
	    return false;
	if (a->device() != b->device())
	    return false;
    }
}

bool
VolumeAddress::overlaps(const VolumeAddress& that) const
{
    if (_rep == that._rep)
	return true;
    if (!_rep || !that._rep)
	return false;
    const PartitionAddress *p;
    for (int i = 0; p = partition(i); i++)
	if (p->overlaps(that))
	    return true;
    return false;
}

const PartitionAddress *
VolumeAddress::as_PartitionAddress() const
{
    return _rep ? _rep->as_PartitionAddress() : NULL;
}

PartitionAddress *
VolumeAddress::as_PartitionAddress()
{
    return _rep ? _rep->as_PartitionAddress() : NULL;
}

VolumeAddress::VolumeAddress(const char *path, FormatIndex fmt, int partno)
: _refcount(0), _rep(NULL)
{
    irixdev_device d;
    int slice;
    if (irixdev_lookup(path, &d, &slice) < 0) return;
    if (partno == UnknownPartition) partno = slice < 0 ? WholeDisk : slice;
    _rep = new PartitionAddress(DeviceAddress(d.controller, d.target, d.lun), fmt, partno);
}
