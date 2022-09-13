/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
   This file contains the main framework for the Quick Emacs manual.

   The manual is composed by scandoc from all source files including this one.
   The sections are extracted from scandoc comments, multiline comments
   starting with a @ and an ordering string. All sections are concatenated
   in natural order of the ordering strings.
 */

/*@ INTRO=0 - introduction
 */
/*@ CONCEPTS=1 - concepts
 */
/*@ BUFFERS=2 - buffers
 */
/*@ WINDOWS-3 - windows
 */
/*@ MODES=4 - modes
 */
/*@ CMD=5 - commands
 */
/*@ IMPL=6 - implementation
 */
/*@ STRUCT=7 - structures
 */
/*@ API=8 - functions
 */
/*@ CREDITS=9 - credits
 */
