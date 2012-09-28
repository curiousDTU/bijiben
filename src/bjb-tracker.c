#include "bjb-tracker.h"

///////////////// Utils.

TrackerSparqlConnection *bjb_connection ;

static TrackerSparqlConnection *
get_connection_singleton(void)
{    
  if ( bjb_connection == NULL )
  {
    GError *error = NULL ;
      
    g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"Getting tracker connection.");
    bjb_connection = tracker_sparql_connection_get (NULL,&error);
      
    if ( error ) 
      g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"Connection errror. Tracker out.");
  }

  return bjb_connection ;
}

static TrackerSparqlCursor *
bjb_perform_query ( gchar * query )
{
  TrackerSparqlCursor * result ;
  GError *error = NULL ;

  result = tracker_sparql_connection_query (get_connection_singleton(),
                                            query,
                                            NULL,
                                            &error);

  if ( error)
  {
    g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"Query error : %s",error->message);
    g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"query was |%s|", query);
  }

  return result ;
}

static void
bjb_perform_update(gchar *query)
{
  GError *error = NULL ;

  tracker_sparql_connection_update (get_connection_singleton(),
                                    query,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    &error);

  if ( error)
  {
    g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"Query error : %s",error->message);
    g_log(G_LOG_DOMAIN,G_LOG_LEVEL_DEBUG,"query was |%s|", query);
  }
}

static gchar *
tracker_str ( gchar * string )
{
  gchar *result = g_strjoinv(" ", g_strsplit(string, "\n", -1));
  return g_strjoinv("\\'", g_strsplit(result, "'", -1));
}

static gchar *
to_8601_date( gchar * dot_iso_8601_date )
{
  gchar *result = dot_iso_8601_date ;
  return g_strdup_printf ( "%sZ",
                           g_utf8_strncpy  (result ,dot_iso_8601_date, 19) );
}

/////////////// Tags

GList *
tracker_tag_get_files(gchar *tag)
{
  GList *result = NULL ;
    
  gchar *query ;
  query = g_strdup_printf( "SELECT ?s nie:url(?s) nie:mimeType(?s) WHERE \
          { ?s a nfo:FileDataObject;nao:hasTag [nao:prefLabel'%s'] }",
                          tracker_str(tag) );
  
  TrackerSparqlCursor *cursor = bjb_perform_query(query) ;
      
  if (!cursor)
  {
    return result ;
  }

  GString * file ;
  while (tracker_sparql_cursor_next (cursor, NULL, NULL))
  {
    file = g_string_new(tracker_sparql_cursor_get_string(cursor,0,NULL));
    result = g_list_append(result,g_string_free(file,FALSE));
  }

  g_object_unref (cursor);
  return result ;
}

// Count files for one given tag. 
gint
tracker_tag_get_number_of_files(gchar *tag)
{      
  gchar *query ;
  query = g_strdup_printf( "SELECT ?s nie:url(?s) nie:mimeType(?s) WHERE \
          { ?s a nfo:FileDataObject;nao:hasTag [nao:prefLabel'%s'] }",
                          tracker_str(tag));

  TrackerSparqlCursor *cursor = bjb_perform_query(query);
  gint result = 0 ;
	
  if (!cursor)
  {
    return result ;
  }
                          
  while (tracker_sparql_cursor_next (cursor, NULL, NULL))
  {
    result++;
  }

  g_object_unref (cursor);
  return result ;
}

GList *
get_all_tracker_tags()
{
  GList *ret = NULL ;

  //  Get all available tags
  gchar *query = "SELECT DISTINCT ?labels WHERE \
  { ?tags a nao:Tag ; nao:prefLabel ?labels. }";

  /* This old version only gets tags which are assigned 
  "SELECT DISTINCT ?labels WHERE {?f nie:isStoredAs ?as ; nao:hasTag ?tags. \
  ?u a nfo:FileDataObject . ?tags a nao:Tag ; nao:prefLabel ?labels. }";     */
	
  TrackerSparqlCursor *cursor = bjb_perform_query( query) ;
    
  if (!cursor)
  {
    g_message ("no result..");
    return NULL ;
  }
      
  else
  {
    GString * tag ; 
    while (tracker_sparql_cursor_next (cursor, NULL, NULL))
    {
      tag = g_string_new(tracker_sparql_cursor_get_string(cursor,0,NULL));   
      ret = g_list_append(ret,(gpointer)g_string_free(tag,FALSE));
    }
        
	g_object_unref (cursor);
  }
    
  return ret;
}

void 
push_tag_to_tracker(gchar *tag)
{ 
  gchar *query = g_strdup_printf ("INSERT {_:tag a nao:Tag ; \
  nao:prefLabel '%s' . } \
  WHERE { OPTIONAL {?tag a nao:Tag ; nao:prefLabel '%s'} . \
  FILTER (!bound(?tag)) }",tag,tag);

  bjb_perform_update (query) ;
}

// removes the tag EVEN if files associated.
void
remove_tag_from_tracker(gchar *tag)
{
  gchar *query = g_strdup_printf ("DELETE { ?tag a nao:Tag } \
  WHERE { ?tag nao:prefLabel '%s' }",tracker_str(tag));

  bjb_perform_update(query);
}

void
push_existing_tag_to_note(gchar *tag,BijiNoteObj *note)
{
  gchar *query = g_strdup_printf( "INSERT { <%s> nao:hasTag ?id } \
  WHERE {   ?id nao:prefLabel '%s' }", get_note_path(note),tag ) ;
    
  bjb_perform_update(query);
}

void
remove_tag_from_note (gchar *tag, BijiNoteObj *note)
{
  gchar *query = g_strdup_printf( "DELETE { <%s> nao:hasTag ?id } \
  WHERE {   ?id nao:prefLabel '%s' }", get_note_path(note),tag ) ;
    
  bjb_perform_update(query); 
}



//
void
biji_note_delete_from_tracker(BijiNoteObj *note)
{
  gchar *query = g_strdup_printf ("DELETE { <%s> a rdfs:Resource }",
                                  get_note_path(note) );

  bjb_perform_update(query);
}

// FIXME add plaintextcontent
static void 
biji_note_create_into_tracker(BijiNoteObj *note)
{
  gchar *query,*title,*content,*file,*create_date,*last_change_date ;
    
  title = tracker_str(biji_note_get_title(note));
  content = tracker_str (biji_note_get_raw_text (note));
  file = g_strdup_printf ("file://%s", get_note_path(note) );
  create_date = to_8601_date(biji_note_obj_get_last_change_date(note));
  last_change_date = to_8601_date(biji_note_obj_get_last_change_date(note));
  content = tracker_str ( _biji_note_obj_get_raw_text(note) );
                          
  query = g_strdup_printf ("INSERT { <%s> a nfo:Note , nie:DataObject ; \
                            nie:url '%s' ; \
                            nie:contentLastModified '%s' ; \
                            nie:contentCreated '%s' ; \
                            nie:title '%s' ; \
                            nie:plainTextContent '%s' ; \
                            nie:generator 'Bijiben' . }",
                           get_note_path(note),
                           file,
                           last_change_date,
                           create_date,
                           title,
                           content) ;

  bjb_perform_update(query);

  g_free(title);
  g_free(file);
  g_free(content); 
  g_free(create_date);
  g_free(last_change_date);
}

/* FIXME PROBABLY FORGET ABOUT THIS UNTILL TRACKER OFFERS THE 'REPLACE' 
static void 
biji_note_update_tracker(BijiNoteObj *note)
{                    
  gchar *query,*uri,*title,*last_change_date,*content ;

  uri = get_note_path(note);
  title = tracker_str(biji_note_get_title(note));
  content = tracker_str ( _biji_note_obj_get_raw_text(note) );
  last_change_date = to_8601_date(biji_note_obj_get_last_change_date(note));

  query = g_strdup_printf( "DELETE { <%s> nie:contentLastModified ?x;\
                            nie:title ?t ; \
                            nie:plainTextContent ?content .}  \
                            WHERE { <%s> a nfo:Note ; \
                            nie:contentLastModified ?x ; \
                            nie:title ?t ; \
                            nie:plainTextContent ?content .} \
                            INSERT { <%s> nie:contentLastModified '%s' ; \
                            nie:title '%s' ; \
                            nie:plainTextContent '%s' .}",
                           uri,
                           uri,
                           uri,
                           last_change_date,
                           title,
                           content) ;

  bjb_perform_update(query);
  g_free(title);
  g_free(last_change_date);
}
*/
            
// TODO (?) add time there, eg is_note_tracked ( BijiNoteObj, GDateTime )
gboolean
is_note_into_tracker ( BijiNoteObj *note )
{
  gchar *query = g_strdup_printf ("SELECT ?modDate WHERE { <%s> a nfo:Note ; \
                                  nie:contentLastModified ?modDate.}",
                                  get_note_path(note));

  TrackerSparqlCursor *cursor = bjb_perform_query(query);
  if (!cursor)
    return FALSE ;

  else
  {
    gboolean result =  tracker_sparql_cursor_next (cursor, NULL, NULL) ;
    g_object_unref (cursor);
    return result;
  }
}

// Either create or update.
void
bijiben_push_note_to_tracker(BijiNoteObj *note)
{
  if ( is_note_into_tracker(note) == TRUE )
    biji_note_delete_from_tracker(note);

  biji_note_create_into_tracker(note);
}
