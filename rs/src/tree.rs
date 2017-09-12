pub struct Tree<T> {
    value:    T,
    children: Vec<Tree<T>>,
}


impl<T> Tree<T> {
    pub fn new(value: T, size_hint: usize) -> Self {
        Tree {
            value:    value,
            children: Vec::with_capacity(size_hint),
        }
    }

    pub fn val(&self) -> &T {
        &self.value
    }

    pub fn add_child(&mut self, child: Self) {
        self.children.place_back() <- child;
    }

    pub fn children(&self) -> &Vec<Tree<T>> {
        &self.children
    }
}

impl<T> Clone for Tree<T> where T: Clone {
    fn clone(&self) -> Self {
        Tree {
            value:    self.value.clone(),
            children: self.children.clone(),
        }
    }
}
