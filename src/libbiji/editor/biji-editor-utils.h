/*
 * e-editor-utils.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_EDITOR_UTILS_H
#define E_EDITOR_UTILS_H

#include <webkit/webkitdom.h>

WebKitDOMElement *	e_editor_dom_node_find_parent_element
							(WebKitDOMNode *node,
							 const gchar *tagname);

WebKitDOMElement *	e_editor_dom_node_find_child_element
							(WebKitDOMNode *node,
							 const gchar *tagname);

#endif /* E_EDITOR_UTILS_H */
