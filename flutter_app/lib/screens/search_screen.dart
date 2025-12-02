import 'package:flutter/material.dart';
import 'package:flutter_app/widgets/header/my_header.dart';
import '../widgets/body/search/filter_card.dart';
import '../widgets/body/search/search_bar.dart';
import '../widgets/footer/bottom_navigation_bar.dart';

class SearchScreen extends StatelessWidget {
  const SearchScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),

      body: SingleChildScrollView(
        //allows scrolling of all content if there is not enough height
        child: Padding(
          padding: const EdgeInsets.all(16.0), //adds space around all content.
          child: Column(
            //starting from top to bottom
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              MyHeader(title: 'Search'),

              const SizedBox(height: 24), //space

              const MySearchBar(),

              const SizedBox(height: 24), //space

              const Text(
                'Filters',
                style: TextStyle(
                  color: Colors.white,
                  fontSize: 20,
                  fontWeight: FontWeight.bold,
                ),
              ),

              const SizedBox(height: 16), //space

              GridView.count(
                //grid of filter squares
                crossAxisCount: 2, //two squares in each row
                crossAxisSpacing: 12, //column spacing
                mainAxisSpacing: 12, //row spacing
                childAspectRatio: 1.1, //the ratio of the width and height
                shrinkWrap: true, //dont take up the entire screen
                physics: const NeverScrollableScrollPhysics(),
                children: const [
                  FilterCard(color: Colors.red, label: 'Internet'),
                  FilterCard(color: Colors.amber, label: 'Global'),
                  FilterCard(color: Colors.green, label: 'Private'),
                  FilterCard(color: Colors.blue, label: '+', centerBig: true),
                ],
              ),
            ],
          ),
        ),
      ),

      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 1),
    );
  }
}
