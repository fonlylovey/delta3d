/*
 * Delta3D Open Source Game and Simulation Engine
 * Copyright (C) 2008 MOVES Institute
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Jeff P. Houde
 */

#include <sstream>
#include <algorithm>

#include <dtDirector/inputlink.h>
#include <dtDirector/outputlink.h>

#include <dtDAL/enginepropertytypes.h>
#include <dtDAL/actorproperty.h>

namespace dtDirector
{
   ///////////////////////////////////////////////////////////////////////////////////////
   InputLink::InputLink(Node* owner, const std::string& name)
      : mVisible(true)
      , mOwner(owner)
   {
      SetName(name);
   }

   ///////////////////////////////////////////////////////////////////////////////////////
   InputLink::~InputLink()
   {
      // Disconnect this link from all outputs.
      Disconnect();
   }

   ////////////////////////////////////////////////////////////////////////////////
   InputLink::InputLink(const InputLink& src)
   {
      mName = src.mName;
      mVisible = src.mVisible;
      mOwner = src.mOwner;

      *this = src;
   }

   ////////////////////////////////////////////////////////////////////////////////
   InputLink& InputLink::operator=(const InputLink& src)
   {
      // Disconnect this link from all outputs.
      Disconnect();

      mName = src.mName;
      mVisible = src.mVisible;
      mOwner = src.mOwner;

      // Now connect this link to all output links connected to by the source.
      int count = (int)src.mLinks.size();
      for (int index = 0; index < count; index++)
      {
         Connect(src.mLinks[index]);
      }

      return *this;
   }

   //////////////////////////////////////////////////////////////////////////
   void InputLink::SetName(const std::string& name)
   {
      mName = name;
   }

   //////////////////////////////////////////////////////////////////////////
   const std::string& InputLink::GetName() const
   {
      return mName;
   }

   //////////////////////////////////////////////////////////////////////////
   bool InputLink::Connect(OutputLink* output)
   {
      if (output) return output->Connect(this);
      return false;
   }

   //////////////////////////////////////////////////////////////////////////
   bool InputLink::Disconnect(OutputLink* output)
   {
      if (!output)
      {
         bool result = false;
         while (!mLinks.empty())
         {
            result |= mLinks[0]->Disconnect(this);
         }

         return result;
      }
      else
      {
         return output->Disconnect(this);
      }

      return false;
   }
}
