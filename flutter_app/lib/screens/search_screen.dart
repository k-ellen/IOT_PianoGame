import 'package:flutter/material.dart';

class SearchScreen extends StatelessWidget {
  const SearchScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),

      body: SafeArea(
        child: LayoutBuilder(  //Gives us information about the size of the area where the screen is drawn
          builder: (context, constraints) {
            final double maxContentWidth =
                constraints.maxWidth > 500 ? 500 : constraints.maxWidth;

            return Center(
              child: ConstrainedBox(  //Limits the width
                constraints: BoxConstraints(maxWidth: maxContentWidth),  
                child: SingleChildScrollView( //allows scrolling of all content if there is not enough height
                  child: Padding(
                    padding: const EdgeInsets.all(16.0), //adds space around all content.
                    child: Column(  //starting from top to bottom
                      crossAxisAlignment: CrossAxisAlignment.start,  
                      children: [
                        Row(  //In the top row there is the word search and the icon
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,  //One is pushed to the left edge the other to the right edge
                          children: [
                            const Text(
                              'Search',
                              style: TextStyle(
                                color: Colors.white,
                                fontSize: 32,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                            ClipRRect(  //Shape with rounded corners
                              borderRadius: BorderRadius.circular(12),
                              child: Image.asset(
                                'assets/images/logo.png',
                                width: 40,
                                height: 40,
                                fit: BoxFit.cover,
                              ),
                            ),
                          ],
                        ),

                        const SizedBox(height: 24),   //space

                        TextField(  //the search bar
                          decoration: InputDecoration(
                            hintText: 'Artists, songs, or podcasts',
                            hintStyle:
                                const TextStyle(color: Colors.grey),
                            prefixIcon: const Icon(
                              Icons.search,
                              color: Colors.white,
                            ),
                            filled: true,
                            fillColor: const Color(0xFF2A2A2A),
                            border: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(16),
                              borderSide: BorderSide.none,
                            ),
                          ),
                          style: const TextStyle(color: Colors.white),
                        ),

                        const SizedBox(height: 24),   //space

                        const Text(
                          'Filters',
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 20,
                            fontWeight: FontWeight.bold,
                          ),
                        ),

                        const SizedBox(height: 16),   //space between the word filter and the squares


                        GridView.count(   //grid of filter squares
                          crossAxisCount: 2,    //two squares in each row
                          crossAxisSpacing: 12, //column spacing
                          mainAxisSpacing: 12,  //row spacing
                          childAspectRatio: 1.1,  //the ratio of the width and height 
                          shrinkWrap: true, //dont take up the entire screen
                          physics:
                              const NeverScrollableScrollPhysics(),
                          children: const [
                            _FilterCard(
                              color: Colors.red,
                              label: 'Internet',
                            ),
                            _FilterCard(
                              color: Colors.amber,
                              label: 'Global',
                            ),
                            _FilterCard(
                              color: Colors.green,
                              label: 'Private',
                            ),
                            _FilterCard(
                              color: Colors.blue,
                              label: '+',
                              centerBig: true,
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            );
          },
        ),
      ),

      bottomNavigationBar: BottomNavigationBar( //Navigation bar located at the bottom of the screen
        backgroundColor: const Color(0xFF000000),
        selectedItemColor: Colors.white,
        unselectedItemColor: Colors.grey,
        currentIndex: 1, 
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.home),
            label: 'Home',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.search),
            label: 'Search',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.cloud_upload),
            label: 'Upload',
          ),
        ],
        onTap: (index) {
          //there will be navigation here



        },
      ),
    );
  }
}

class _FilterCard extends StatelessWidget { ////draws one square from the filter squares
  final Color color;
  final String label;
  final bool centerBig;

  const _FilterCard({
    required this.color,
    required this.label,
    this.centerBig = false,
  });

  @override
  Widget build(BuildContext context) {    
    return Container(
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Center(
        child: Text(
          label,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: Colors.black,
            fontSize: centerBig ? 26 : 16,
            fontWeight: FontWeight.bold,
          ),
        ),
      ),
    );
  }
}
