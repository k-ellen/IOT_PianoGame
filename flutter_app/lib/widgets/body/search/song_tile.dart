import 'package:flutter/material.dart';

class SongTile extends StatelessWidget {
  final String title;
  final String artist;   
  final String genre;          
  final int index;       //the row number in the list to select a color by position
  final VoidCallback onTap;

  const SongTile({
    super.key,
    required this.title,
    required this.artist,
    required this.genre,   
    required this.index,
    required this.onTap,
  });

  static const List<Color> colorsList = [  //different colors that will repeat themselves in the list
    Color(0xFFFFD54F), 
    Color(0xFFFF6E6E), 
    Color(0xFF4CAF50), 
    Color(0xFF42A5F5), 
  ];

  @override
  Widget build(BuildContext context) {
    final Color tileColor = colorsList[index % colorsList.length];  //color loop

    return InkWell(
      onTap: onTap,
      child: Container(
        margin: const EdgeInsets.symmetric(vertical: 8),
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Row(
          children: [
            Container(  //the colored square
              width: 56,
              height: 56,
              decoration: BoxDecoration(
                color: tileColor,
                borderRadius: BorderRadius.circular(12),
              ),
              child: const Icon(
                Icons.music_note,
                size: 32,
                color: Colors.black,
              ),
            ),

            const SizedBox(width: 16),  //space so that the text does not stick to the square

            Expanded(   //the text takes up all the space left in the line
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,   //aligned to the left
                children: [
                  Text(
                    title,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 4),

                  Text(
                    artist,        
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white70,
                      fontSize: 14,
                    ),
                  ),

                  const SizedBox(height: 2),

                  Text(
                    genre,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white38,
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
