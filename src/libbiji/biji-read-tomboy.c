#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <libxml/xmlreader.h>

#include <string.h> // TODO get rid of string.h

#include "biji-note-buffer.h"
#include "biji-read-tomboy.h"
#include "libbiji.h"

#define ELEMENT_IS(name) (g_strcmp0 (element_name, (name)) == 0)

// Deserialization 

xmlTextReader *__xml_text_reader = NULL;

xmlTextReader*
xml_get_reader_for_memory(const gchar *xml_string)
{
	// Try to reuse an existing xml parser
	if (__xml_text_reader == NULL) {
		__xml_text_reader = xmlReaderForMemory(xml_string, strlen(xml_string), "", "UTF-8", 0);
		return __xml_text_reader;
	}

	if (xmlReaderNewMemory(__xml_text_reader, xml_string, strlen(xml_string), "", "UTF-8", 0) != 0) {
		g_printerr("ERROR: Couldn't reuse xml parser. \n");
		g_assert_not_reached();
	}

	if (__xml_text_reader == NULL) {
		g_printerr("ERROR: Couldn't init xml parser.\n");
		g_assert_not_reached();
	}

	return __xml_text_reader;
}

typedef enum
{
	STATE_START,
	STATE_CONTENT,
	STATE_LIST,
	STATE_LIST_ITEM
} ParseState;

typedef struct
{
	GSList *tag_stack;
	GSList *state_stack;
	GtkTextIter *iter;
	gint depth;
} ParseContext;

static void 
push_state(ParseContext  *ctx, ParseState  state)
{
  ctx->state_stack = g_slist_prepend(ctx->state_stack, GINT_TO_POINTER(state));
}

static void 
pop_state(ParseContext *ctx)
{
  g_return_if_fail(ctx->state_stack != NULL);
  ctx->state_stack = g_slist_remove(ctx->state_stack, ctx->state_stack->data);
}

static ParseState 
peek_state(ParseContext *ctx)
{
  g_return_val_if_fail(ctx->state_stack != NULL, STATE_START);
  return GPOINTER_TO_INT(ctx->state_stack->data);
}

static void 
push_tag(ParseContext *ctx, gchar *tag_name)
{
  ctx->tag_stack = g_slist_prepend(ctx->tag_stack, tag_name);
}

static void 
pop_tag(ParseContext *ctx)
{
  g_return_if_fail(ctx->tag_stack != NULL);
  ctx->tag_stack = g_slist_remove(ctx->tag_stack, ctx->tag_stack->data);
}

static gchar* 
peek_tag(ParseContext *ctx)
{
  g_return_val_if_fail(ctx->tag_stack != NULL, NULL);
  return ctx->tag_stack->data;
}

static void 
handle_start_element(ParseContext *ctx, xmlTextReader *reader)
{
	const gchar *element_name = (const gchar *) xmlTextReaderConstName(reader);

	switch (peek_state(ctx)) 
	{
	case STATE_START:
		if (ELEMENT_IS("note-content")) 
		{
			push_state(ctx, STATE_CONTENT);
		} 
		else 
		{
			g_printerr("ERROR: First element must be <note-content>, not <%s> \n", element_name);
		}
		break;
            
	case STATE_CONTENT:
	case STATE_LIST_ITEM:
		if (ELEMENT_IS("list"))
		{
			push_state(ctx, STATE_LIST);
			ctx->depth++;
			break;
		}
		push_tag(ctx, (gchar *)element_name);
		break;

	case STATE_LIST:
		if (ELEMENT_IS("list-item")) 
		{
			push_state(ctx, STATE_LIST_ITEM);
		} 
		else
		{
			g_printerr("ERROR: <list> may only contain <list-item>, not <%s>.", element_name);
		}
		break;

	default:
		g_assert_not_reached();
		break;
	}
}

static void 
handle_end_element(ParseContext *ctx, xmlTextReader *reader) 
{
	const gchar *element_name = (const gchar *) xmlTextReaderConstName(reader);

	switch (peek_state(ctx))
	{
	case STATE_CONTENT:
		if (ELEMENT_IS("note-content"))
		{
			pop_state(ctx);
		} 
		else 
		{
			pop_tag(ctx);
		}
		break;

	case STATE_LIST:
		if (ELEMENT_IS("list")) 
		{
			pop_state(ctx);
			ctx->depth--;
		}
		break;

	case STATE_LIST_ITEM:
		if (ELEMENT_IS("list-item"))
		{
			pop_state(ctx);
			g_assert(peek_state(ctx) == STATE_LIST);
		} 
		else 
		{
			pop_tag(ctx);
		}
		break;

	default:
		g_assert_not_reached ();
		break;
	}
}

static GtkTextTag* 
get_depth_tag(ParseContext *ctx, GtkTextBuffer *buffer, gchar* name) 
{
  GtkTextTag *tag;
  gchar depth[5] = {0};
  gchar *tag_name;

  g_sprintf(depth, "%i", ctx->depth);
  tag_name = g_strconcat(name, ":", depth, NULL);

  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  tag = gtk_text_tag_table_lookup (table,tag_name);
  // if the tag does not exist, create it.
  // TODO : instead use write a func in note-tag to create a NoteTag
  if (tag == NULL)
  {
    tag = gtk_text_buffer_create_tag(buffer, 
	                             tag_name, 
	                             "indent", 
	                             -20, 
	                             "left-margin", 
	                             ctx->depth * 25, 
	                             NULL);
    //gtk_text_tag_set_priority
  }

  g_free(tag_name);
  return tag;
}

static void
apply_tags(GSList *tags, GtkTextBuffer *buffer, GtkTextIter *start_iter, GtkTextIter *end_iter)
{
	GtkTextTag *tag;
	GSList *tag_list = tags;
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
		
	while (tag_list) 
	{
		tag = gtk_text_tag_table_lookup(table,tag_list->data);
		if (tag == NULL)
		{
			gchar *tag_name = (gchar*)tag_list->data;
			if (tag_name != NULL && strcmp(tag_name, "") != 0)
			{
				g_printerr("INFO: XML tag <%s> does not exist yet. Creating it.\n",
				           tag_name);
				gtk_text_buffer_create_tag(buffer, tag_name, NULL);
				GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
				tag = gtk_text_tag_table_lookup(table, tag_name);
			}
		}
		gtk_text_buffer_apply_tag(buffer, tag, start_iter, end_iter);
		tag_list = tag_list->next;
	}
}

static void
handle_text_element(ParseContext *ctx, xmlTextReader *reader, GtkTextBuffer *buffer)
{
	const gchar *text = (const gchar *) xmlTextReaderConstValue(reader);
	GtkTextIter *iter = ctx->iter;
	GtkTextIter start_iter;
	GtkTextMark *mark;
	GtkTextTag *depth_tag;
	gboolean bullet_was_inserted;

	switch (peek_state(ctx))
	{

	case STATE_CONTENT:
		mark = gtk_text_buffer_create_mark(buffer, "insert_point", iter, TRUE);
		gtk_text_buffer_insert(buffer, iter, text, -1);

		gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, mark);
		apply_tags(ctx->tag_stack, buffer, &start_iter, iter);
		gtk_text_buffer_delete_mark(buffer, mark);

		break;

	case STATE_LIST:
		// Ignore text in <list> elements - do nothing
		break;

	case STATE_LIST_ITEM:
		bullet_was_inserted = FALSE;

		// Save the line number where we start inserting	
		mark = gtk_text_buffer_create_mark(buffer, "insert_point", iter, TRUE);

		// Insert bullet only if we are at the very beginning of a line
		depth_tag = get_depth_tag(ctx, buffer, "depth");
		if (gtk_text_iter_get_line_offset(iter) == 0) {
			gtk_text_buffer_insert_with_tags(buffer,
			                                 iter,
			                                 get_bullet_by_depth(ctx->depth),
			                                 -1,
			                                 depth_tag, NULL);
			bullet_was_inserted = TRUE;
		}

		// Insert the text into the buffer with list-item tags
		gtk_text_buffer_insert_with_tags_by_name(buffer, iter, text, -1, "list-item", NULL);

		// Apply <list> tag to the complete line, incuding the bullet
		gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, mark);
		gtk_text_buffer_apply_tag_by_name(buffer, "list", &start_iter, iter);

		// Now move start_iter behind BULLET character, 
	  // because we don't want to format the bullet
		if (bullet_was_inserted) {
			gtk_text_iter_forward_chars(&start_iter, 2);
		}

		// Apply the rest of the tags
		apply_tags(ctx->tag_stack, buffer, &start_iter, iter);

		gtk_text_buffer_delete_mark(buffer, mark);
		break;

	default:
		g_printerr("ERROR: Wrong state: %i  Wrong tag: %s\n", peek_state(ctx), peek_tag(ctx));
		g_assert_not_reached();
		break;
	}
}


static
void process_note(ParseContext *ctx, xmlTextReader *reader, GtkTextBuffer *buffer)
{
	int type;
	const xmlChar *name ;

	type  = xmlTextReaderNodeType(reader);
	name  = xmlTextReaderConstName(reader);
	if (name == NULL) {
		name = BAD_CAST "(NULL)";
	}

	switch(type) {
	case XML_ELEMENT_NODE:
		handle_start_element(ctx, reader);
		break;

	case XML_ELEMENT_DECL:
		handle_end_element(ctx, reader);
		break;

	case XML_TEXT_NODE:
		handle_text_element(ctx, reader, buffer);
		break;

	case XML_DTD_NODE:
		handle_text_element(ctx, reader, buffer);
		break;
	}
}

static ParseContext* 
init_parse_context(GtkTextBuffer *buffer, GtkTextIter *iter)
{
  ParseContext *ctx = g_new0(ParseContext, 1);
  ctx->state_stack = g_slist_prepend(NULL, GINT_TO_POINTER(STATE_START));
  ctx->tag_stack = NULL;
  gtk_text_buffer_get_iter_at_offset(buffer, iter, 0);
  ctx->iter = iter;
  ctx->depth = 0;
  return ctx;
}

static void 
destroy_parse_context(ParseContext *ctx) 
{
  ctx->iter = NULL;
  g_slist_free(ctx->state_stack);
  g_slist_free(ctx->tag_stack);
  g_free(ctx);
}

void
note_buffer_set_xml(gpointer note_buffer, const gchar *xml_string)
{		
  BijiNoteBuffer *self = BIJI_NOTE_BUFFER(note_buffer);
		
  g_return_if_fail(self != NULL);
  g_return_if_fail(xml_string != NULL);
  g_return_if_fail(BIJI_IS_NOTE_BUFFER(self));

  int ret;
  ParseContext *ctx;
  GtkTextIter iter;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER(self);
  xmlTextReader *reader = xml_get_reader_for_memory(xml_string);

  // Clear text buffer
  gtk_text_buffer_set_text(buffer, "", -1);
  ctx = init_parse_context(buffer, &iter);
  ret = xmlTextReaderRead(reader);
		
  while (ret == 1) 
  {
    process_note(ctx, reader, buffer);
    ret = xmlTextReaderRead(reader);
  }

  if (ret != 0) 
  {
    g_printerr("ERROR: Failed to parse content.\n");
  }

  destroy_parse_context(ctx);
}


////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
// tomboy deserialize but re-written from scratch

/*
typedef struct
{
	int start;
	GtkTextTag *tag;
} TagStart ;

void
note_buffer_deserialize(GtkTextBuffer *buffer,const gchar *xml_string)
{
	// CONTAINERS AND INT
	gint offset,curr_depth,string_lenght;
	GQueue *list_stack, *tag_stack;
	TagStart *tag_start;

	// XML HANDLING
	xmlTextReader *reader;
	gchar *name; // the xml element name
	xmlChar *value;

	// GTK BUFFER 
	GtkTextTagTable *table;
	GtkTextIter start_iter,insert_at;

	// Prepare the containers
	tag_stack = g_queue_new() ;
	list_stack = g_queue_new() ;

	// Get the XMl reader
	reader = xml_get_reader_for_memory(xml_string);
		
	//  Clear the buffer, get start , get table, get offset
	gtk_text_buffer_set_text(buffer, "", -1);
  gtk_text_buffer_get_start_iter(buffer,&start_iter);
	table = gtk_text_buffer_get_tag_table(buffer);
	curr_depth = 0 ; // TOMBOY DOES curr_depth = -1; Ouch.
	offset = gtk_text_iter_get_offset(&start_iter);

	while(xmlTextReaderRead(reader))
  {
		switch(xmlTextReaderNodeType(reader))
		{
      case XML_READER_TYPE_ELEMENT:
				name = (gchar *) xmlTextReaderName(reader);
				if ( g_strcmp0 ( name,"note-content")==0) {
					g_message("handle start : note-content");
				  break; }

				// should reset tag start first... tag_start = TagStart()
				tag_start = g_new0(TagStart,1); // when to free ???
				tag_start->start = offset;

				// if dynamic tag, create it...
				// else..
				if ( g_strcmp0 ( name,"list")==0) {
				  g_message("handle start : list");
				  curr_depth ++;
				  break ; }

				if ( g_strcmp0 ( name,"list-item")==0) {
				  g_message("handle start : list-item");

					if ( curr_depth >= 0 ) {
						GtkTextTag *tag;
					  // we're supposed to check if LTR or RTL before writing.
						if (TRUE ) // if LTR 
						{ 
					    tag = note_buffer_get_depth_tag(buffer,curr_depth);
  						if (tag == NULL)
						  {
		            g_message("We have to create depth tag  %i",curr_depth);
								
		            gtk_text_buffer_create_tag(buffer, 
		            						               "depth:1", // FIXME!! 
		                                       //g_strdup_printf("depth:%i",curr_depth), // FIXME free each depth tag name?
		            						               "indent", 
		            						               -20, 
		            						               "left-margin", 
		            						               curr_depth * 25, 
		            						               NULL);
									// + gtk_text_tag_set_priority ?
						   }						   
						}

						else {
							// RTL
						}

						tag_start->tag = tag;
					  g_queue_push_head (list_stack,GINT_TO_POINTER(0)); // FALSE
				  }

					else { g_message ("boulala y'a erreur sur les list item!") ; }
				}

				else {
					g_message("handle start : other tag than note-content, list, list-item");
					tag_start->tag = gtk_text_tag_table_lookup(table,name);
				}

				// TODO
			  //if (NoteTag::Ptr::cast_dynamic(tag_start.tag)) {
        //  NoteTag::Ptr::cast_dynamic(tag_start.tag)->read (xml, true);
        //}
				

				g_queue_push_head(tag_stack,tag_start);// tag_stack.push (tag_start);
				break ;

				// end of handling start element . This part is done.

			case XML_READER_TYPE_TEXT:
			case XML_READER_TYPE_WHITESPACE:
      case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:
				g_message("handle text");
				gtk_text_buffer_get_iter_at_offset(buffer,&insert_at,offset);
				value = xmlTextReaderValue(reader);
				string_lenght = g_utf8_strlen((gchar*)value,-1);
				gtk_text_buffer_insert(buffer,&insert_at,(gchar*)value,string_lenght);
				offset += string_lenght;
        
				// get all bool list , but replace the first element with true
				if ( !g_queue_is_empty (list_stack) )
				{
					g_queue_pop_head(list_stack); //list_stack.pop_front ();
					g_queue_push_head(list_stack,GINT_TO_POINTER(1)); // TRUE ; list_stack.push_front (true);
				}
				
				break;

						// end of text, then start of end :) ain't a poem ?

			case XML_READER_TYPE_END_ELEMENT:
				name = (gchar *) xmlTextReaderName(reader);

				if ( g_strcmp0 ( name,"note-content")==0) {
				g_message("handle end : note-content");
				break; }

				if ( g_strcmp0 ( name,"list")==0) {
				g_message("handle end element : list");
				curr_depth--;
				break; }

				g_message("handling other end element");
				tag_start = g_queue_peek_head(tag_stack);
				g_queue_pop_head(tag_stack);

				if ( tag_start->tag ) {
				  GtkTextIter apply_start,apply_end ;
					gtk_text_buffer_get_iter_at_offset(buffer,&apply_start,tag_start->start);
					gtk_text_buffer_get_iter_at_offset(buffer,&apply_end,offset);

					// TODO
					//            if (NoteTag::Ptr::cast_dynamic(tag_start.tag)) {
          //    NoteTag::Ptr::cast_dynamic(tag_start.tag)->read (xml, false);
          //  }
					

				  // Insert a bullet if we have reached a closing
          // <list-item> tag, but only if the <list-item>
          // had content.
					//TODO

					gtk_text_buffer_apply_tag(buffer,tag_start->tag,&apply_start,&apply_end);
				}

				break;
				
			default:
				g_message("node type not handled yet:%i",xmlTextReaderNodeType(reader));
				break;
		}
		g_message("Element handled.");		
	}
	g_message("end of deserialization.");
	/// TODO free each g_queue element (tag_stack)
	// g_queue_foreach (tag_stack,(GFunc)g_free,NULL); // SEGFAULT
	g_queue_free(tag_stack);
  g_queue_free(list_stack);
} */

//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
// Metadata handling

// TODO :cursor position, Selection bound position, 
//	  Width,Height,x,y,open-on-startup
// TODO : date format 
static void
processNode(xmlTextReaderPtr r, BijiNoteObj * n) 
{
  xmlChar *name;
  name = xmlTextReaderName(r);

  if ( g_strcmp0((gchar*)name,"title") == 0 )  
  {
    set_note_title(n, (gchar*) xmlTextReaderReadString(r));
  }

  if ( g_strcmp0((gchar*)name,"text") == 0 )  
  {
    biji_note_set_content(n, (gchar*)xmlTextReaderReadInnerXml(r));
  }

  if ( g_strcmp0((gchar*)name,"last-change-date") == 0 )  
  {
    set_note_last_change_date (n,(gchar*) xmlTextReaderReadString(r)) ;  
  }

  if ( g_strcmp0((gchar*)name,"last-metadata-change-date") == 0 )  
  {
    set_note_last_metadata_change_date(n,(gchar*) xmlTextReaderReadString(r)) ;
  }

  if ( g_strcmp0((gchar*)name,"create-date") == 0 )  
  {
    set_note_create_date (n,(gchar*) xmlTextReaderReadString(r));
  }

  if ( g_strcmp0((gchar*)name,"color") == 0 )  
  {
    note_obj_set_rgba (n,(gchar*) xmlTextReaderReadString(r));
  }

  if ( g_strcmp0((gchar*)name,"tag") == 0 )  
  {
    gchar * tag = (gchar*) xmlTextReaderReadString(r);

    if (g_str_has_prefix (tag,"system:template"))
    {
      note_obj_set_is_template(n,TRUE);
    }

    else if (g_str_has_prefix (tag,"system:notebook:"))
    {
      GString * norm = g_string_new(tag);
      g_string_erase(norm,0,16);
      tag = g_string_free(norm,FALSE);
      _biji_note_obj_set_tags(n,g_list_append((GList*)_biji_note_obj_get_tags(n),
                                            (gpointer)tag));
    }
  }
}

/* just test node type. might be integrated
 * http://www.gnu.org/software/dotgnu/pnetlib-doc/System/Xml/XmlNodeType.html */
static void
biji_parse_file(BijiNoteObj * note,xmlTextReaderPtr r)
{	
  while ( xmlTextReaderRead(r) == 1 )
  {
    if ( xmlTextReaderNodeType(r) == 1 )
    {
      processNode(r,note);
    }
  }
}

int
load_tomboy_note (gpointer note)
{
  BijiNoteObj* n = (BijiNoteObj*) note ;
  xmlDocPtr doc;
  xmlNodePtr cur;
  doc = xmlParseFile(get_note_path(n));

  if (doc == NULL ) 
  {
    g_message("File not parsed successfully. \n");
    return FALSE;
  }

  cur = xmlDocGetRootElement(doc);
  if (cur == NULL) 
  {
    g_message("File empty\n");
    xmlFreeDoc(doc);
    return FALSE;
  }

  if (xmlStrcmp(cur->name, (const xmlChar *) "note")) 
  {
    g_message("document of the wrong type, root node != note");
    xmlFreeDoc(doc);
    return FALSE;
  }

  xmlTextReaderPtr r = xmlNewTextReaderFilename(get_note_path(n));
  biji_parse_file(n,r);
  xmlFreeDoc(doc);
  return TRUE ;
}