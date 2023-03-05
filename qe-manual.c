/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2023 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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
