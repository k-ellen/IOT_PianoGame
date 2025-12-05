import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';



//Scans the entire "Global Songs" folder in Firebase Storage
//inds all MIDI files within it and imports them into Firestore as documents in the songs collection
Future<void> importGlobalSongsToFirestore() async {
  final firestore = FirebaseFirestore.instance;
  final storage = FirebaseStorage.instance;

  final rootRef = storage.ref('Global Songs');


  //A recursive function that Scans a folder in Firebase Storage finds MIDI files in it
  // and adds their information to Firestore
  Future<void> walkFolder(Reference folderRef) async {    //walkFolder receives a folder and scans it
    final listResult = await folderRef.listAll();

    for (final fileRef in listResult.items) {
      final fullPath = fileRef.fullPath; 
      final parts = fullPath.split('/');    //Parses the path into a list, for exemple parts = ["Global Songs", "Classical", "Mozart", "song1.mid"]

      if (parts.length < 4) continue;

      final fileName = parts.last;    //The name of the file itself
      if (!fileName.toLowerCase().endsWith('.mid')) continue;

      final genreName  = parts[1];                      
      final artistName = parts[parts.length - 2];     //The folder that is one before the end    
      final storagePath = fullPath;                  

      final displayName = fileName.substring(0, fileName.length - 4);   //The song's call name, without .midi

      final existing = await firestore
          .collection('songs')
          .where('storagePath', isEqualTo: storagePath)
          .limit(1)
          .get();

      if (existing.docs.isNotEmpty) continue;   //If you have already imported this song it will not be duplicated

      await firestore.collection('songs').add({   //A new document has been added to the songs collection
        'name': displayName,
        'artist': artistName,    
        'genre': genreName,     
        'storagePath': storagePath,
        'source': 'global',
      });

      print('Added: $displayName  [$genreName / $artistName]');
    }

    for (final subFolder in listResult.prefixes) {
      await walkFolder(subFolder);    //Looping over subfolders recursion
    }
  }

  await walkFolder(rootRef);   //Responsible for first running the walkFolder function on rootRef = Global Songs

  print('Import finished');
}
