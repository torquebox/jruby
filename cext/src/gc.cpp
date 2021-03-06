 /***** BEGIN LICENSE BLOCK *****
 * Version: CPL 1.0/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Common Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.eclipse.org/legal/cpl-v10.html
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2010 Wayne Meissner
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the CPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the CPL, the GPL or the LGPL.
 ***** END LICENSE BLOCK *****/

#include <vector>
#include <list>
#include "jruby.h"
#include "ruby.h"
#include "Handle.h"
#include "JLocalEnv.h"

using namespace jruby;

static std::list<VALUE*> globalVariables;

extern "C" void
rb_gc_mark_locations(VALUE* first, VALUE* last)
{
    for (VALUE* vp = first; vp < last; ++vp) {
        rb_gc_mark(*vp);
    }
}

extern "C" void
rb_gc_mark(VALUE v)
{
    if (SPECIAL_CONST_P(v)) {
        // special constant, ignore
        return;
    }

    Handle* h = Handle::valueOf(v);
    if ((h->flags & FL_MARK) == 0) {
        h->flags |= FL_MARK;
    }
}

extern "C" void
rb_gc_mark_maybe(VALUE v)
{
    if (SPECIAL_CONST_P(v)) {
        return;
    }

    Handle* h;
    TAILQ_FOREACH(h, &liveHandles, all) {
        if ((VALUE) h == v) {
            rb_gc_mark(v);
            break;
        }
    }
}

extern "C" void
rb_gc_register_address(VALUE *addr)
{
    globalVariables.push_back(addr);
}

extern "C" void
rb_gc_unregister_address(VALUE *addr)
{
    globalVariables.remove(addr);
}

extern "C" void
rb_global_variable(VALUE *var)
{
    rb_gc_register_address(var);
}

/*
 * Class:     org_jruby_cext_Native
 * Method:    gc
 * Signature: ()V
 */
extern "C" JNIEXPORT void JNICALL
Java_org_jruby_cext_Native_gc(JNIEnv* env, jobject self)
{
    RubyData* dh;
    Handle* h;

    TAILQ_FOREACH(dh, &dataHandles, dataList) {
        RData* rdata = dh->toRData();
        if ((dh->flags & FL_MARK) == 0 && rdata->dmark != NULL) {
            dh->flags |= FL_MARK;
            (*rdata->dmark)(rdata->data);
            dh->flags &= ~FL_MARK;
        }
    }

    /*
     * Set the mark flag on all global vars, so they don't get pruned out
     */
    for (std::list<VALUE*>::iterator it = globalVariables.begin(); it != globalVariables.end(); ++it) {
        VALUE* vp = *it;
        if (vp != NULL && !SPECIAL_CONST_P(*vp)) {
            reinterpret_cast<Handle*>(*vp)->flags |= FL_MARK;
        }
    }

    for (h = TAILQ_FIRST(&liveHandles); h != TAILQ_END(&liveHandles); ) {
        Handle* next = TAILQ_NEXT(h, all);

        if ((h->flags & (FL_MARK | FL_CONST)) == 0) {

            if (unlikely(h->getType() == T_DATA)) {
                if ((h->flags & FL_WEAK) == 0) {
                    h->flags |= FL_WEAK;
                    jobject obj = env->NewWeakGlobalRef(h->obj);
                    env->DeleteGlobalRef(h->obj);
                    h->obj = obj;
                }

            } else {
                TAILQ_REMOVE(&liveHandles, h, all);
                TAILQ_INSERT_TAIL(&deadHandles, h, all);
            }

        } else if ((h->flags & FL_MARK) != 0) {
            h->flags &= ~FL_MARK;
        }

        h = next;
    }
}

/*
 * Class:     org_jruby_cext_Native
 * Method:    pollGC
 * Signature: ()Ljava/lang/Object;
 */
extern "C" JNIEXPORT jobject JNICALL
Java_org_jruby_cext_Native_pollGC(JNIEnv* env, jobject self)
{
    Handle* h = TAILQ_FIRST(&deadHandles);
    if (h == TAILQ_END(&deadHandles)) {
        return NULL;
    }
    TAILQ_REMOVE(&deadHandles, h, all);

    jobject obj = env->NewLocalRef(h->obj);
    env->DeleteGlobalRef(h->obj);
    delete h;

    return obj;
}
