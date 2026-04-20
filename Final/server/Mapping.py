import tkinter as tk
from tkinter import ttk, messagebox
import pandas as pd
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

class MapPathfinderUI:
    def __init__(self, root, csv_path):
        self.root = root
        self.root.title("Map Shortest Path Visualizer (with LRFU & Full Explore)")
        self.root.geometry("1000x850")
        
        # Load Data
        try:
            self.G, self.pos = self.load_graph_data(csv_path)
            self.nodes = list(self.G.nodes())
            self.nodes.sort()
        except Exception as e:
            messagebox.showerror("Error", f"Could not load CSV data:\n{e}")
            self.root.destroy()
            return

        # Setup UI Frames
        self.setup_ui()
        
        # Initial Draw
        self.draw_graph()

    def load_graph_data(self, csv_file):
        """Loads the CSV, builds the graph, and calculates node coordinates."""
        df = pd.read_csv(csv_file)
        G = nx.Graph()
        
        for _, row in df.iterrows():
            idx = int(row['index'])
            G.add_node(idx)

        for _, row in df.iterrows():
            u = int(row['index'])
            dirs = [('North', 'ND'), ('South', 'SD'), ('West', 'WD'), ('East', 'ED')]
            for d_name, d_dist_col in dirs:
                v = row[d_name]
                if pd.notna(v):
                    v = int(v)
                    dist = row[d_dist_col] if pd.notna(row[d_dist_col]) else 1
                    G.add_edge(u, v, weight=dist)

        # BFS to find coordinates
        pos = {}
        start_node = int(df['index'].iloc[0])
        pos[start_node] = (0, 0)
        
        queue = [start_node]
        visited = {start_node}
        
        while queue:
            u = queue.pop(0)
            ux, uy = pos[u]
            row = df[df['index'] == u].iloc[0]
            
            dirs_coord = [('North', 0, 1, 'ND'), ('South', 0, -1, 'SD'), ('West', -1, 0, 'WD'), ('East', 1, 0, 'ED')]
            for d_name, dx, dy, d_dist_col in dirs_coord:
                v = row[d_name]
                if pd.notna(v):
                    v = int(v)
                    dist = row[d_dist_col] if pd.notna(row[d_dist_col]) else 1
                    vx, vy = ux + dx * dist, uy + dy * dist
                    if v not in pos:
                        pos[v] = (vx, vy)
                        if v not in visited:
                            visited.add(v)
                            queue.append(v)
                            
        return G, pos
    
    @staticmethod
    def load_graph_data_static(csv_file):
        """Static method to load the CSV, build the graph, and calculate node coordinates."""
        df = pd.read_csv(csv_file)
        G = nx.Graph()
        
        for _, row in df.iterrows():
            idx = int(row['index'])
            G.add_node(idx)

        for _, row in df.iterrows():
            u = int(row['index'])
            dirs = [('North', 'ND'), ('South', 'SD'), ('West', 'WD'), ('East', 'ED')]
            for d_name, d_dist_col in dirs:
                v = row[d_name]
                if pd.notna(v):
                    v = int(v)
                    dist = row[d_dist_col] if pd.notna(row[d_dist_col]) else 1
                    G.add_edge(u, v, weight=dist)

        # BFS to find coordinates
        pos = {}
        start_node = int(df['index'].iloc[0])
        pos[start_node] = (0, 0)
        
        queue = [start_node]
        visited = {start_node}
        
        while queue:
            u = queue.pop(0)
            ux, uy = pos[u]
            row = df[df['index'] == u].iloc[0]
            
            dirs_coord = [('North', 0, 1, 'ND'), ('South', 0, -1, 'SD'), ('West', -1, 0, 'WD'), ('East', 1, 0, 'ED')]
            for d_name, dx, dy, d_dist_col in dirs_coord:
                v = row[d_name]
                if pd.notna(v):
                    v = int(v)
                    dist = row[d_dist_col] if pd.notna(row[d_dist_col]) else 1
                    vx, vy = ux + dx * dist, uy + dy * dist
                    if v not in pos:
                        pos[v] = (vx, vy)
                        if v not in visited:
                            visited.add(v)
                            queue.append(v)
                            
        return G, pos

    def setup_ui(self):
        """Creates the controls and the canvas for the plot."""
        # Top Control Panel
        control_frame = ttk.Frame(self.root, padding=10)
        control_frame.pack(side=tk.TOP, fill=tk.X)

        # Row 1: Controls
        row1 = ttk.Frame(control_frame)
        row1.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(row1, text="Start Node:").pack(side=tk.LEFT, padx=5)
        self.start_combo = ttk.Combobox(row1, values=self.nodes, width=10, state="readonly")
        self.start_combo.pack(side=tk.LEFT, padx=5)
        if self.nodes:
            self.start_combo.set(self.nodes[0])

        ttk.Label(row1, text="End Node:").pack(side=tk.LEFT, padx=5)
        self.end_combo = ttk.Combobox(row1, values=self.nodes, width=10, state="readonly")
        self.end_combo.pack(side=tk.LEFT, padx=5)
        if self.nodes:
            self.end_combo.set(self.nodes[-1])

        self.calc_button = ttk.Button(row1, text="Find Shortest Path", command=self.calculate_path)
        self.calc_button.pack(side=tk.LEFT, padx=10)
        
        # === NEW BUTTON: Explore Full Map ===
        self.explore_button = ttk.Button(row1, text="Explore Full Map", command=self.explore_map)
        self.explore_button.pack(side=tk.LEFT, padx=10)
        
        self.reset_button = ttk.Button(row1, text="Reset Map", command=self.reset_map)
        self.reset_button.pack(side=tk.LEFT, padx=5)

        # Row 2: Info and LRFU outputs
        row2 = ttk.Frame(control_frame)
        row2.pack(side=tk.TOP, fill=tk.X, pady=10)
        
        self.info_label = ttk.Label(row2, text="", foreground="white", font=("Arial", 11, "bold"), wraplength=950)
        self.info_label.pack(side=tk.LEFT, padx=5)

        # Plot Canvas Area
        self.fig, self.ax = plt.subplots(figsize=(10, 8))
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.root)
        self.canvas.get_tk_widget().pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True)

    def draw_graph(self, path=None):
        """Draws the network graph on the matplotlib canvas."""
        self.ax.clear()
        
        # Base map styles
        node_color = 'lightgray'
        edge_color = 'lightgray'
        alpha = 0.5
        
        if not path:
            node_color = 'lightblue'
            edge_color = 'gray'
            alpha = 1.0

        # Draw Base Graph
        nx.draw_networkx_nodes(self.G, self.pos, ax=self.ax, node_size=500, node_color=node_color, alpha=alpha)
        nx.draw_networkx_edges(self.G, self.pos, ax=self.ax, width=1, edge_color=edge_color, alpha=alpha)
        nx.draw_networkx_labels(self.G, self.pos, ax=self.ax, font_size=9)

        # Draw Highlighted Path
        if path:
            path_edges = list(zip(path, path[1:]))
            nx.draw_networkx_edges(self.G, self.pos, ax=self.ax, edgelist=path_edges, width=4, edge_color='red')
            nx.draw_networkx_nodes(self.G, self.pos, ax=self.ax, nodelist=path, node_size=600, node_color='orange')
            
            # Start and End Nodes
            nx.draw_networkx_nodes(self.G, self.pos, ax=self.ax, nodelist=[path[0]], node_size=800, node_color='green')
            nx.draw_networkx_nodes(self.G, self.pos, ax=self.ax, nodelist=[path[-1]], node_size=800, node_color='blue')
            
            self.ax.set_title(f'Path Visualization: {path[0]} → {path[-1]}', fontsize=14, pad=10)
        else:
            self.ax.set_title("Full Map Overview", fontsize=14, pad=10)

        self.ax.axis('off')
        self.fig.tight_layout()
        self.canvas.draw()

    def get_lrfu_sequence(self, path):
        """Converts node sequence into a Left/Right/Forward/U-turn string."""
        if not path or len(path) < 2:
            return []

        def get_cardinal_dir(u, v):
            x1, y1 = self.pos[u]
            x2, y2 = self.pos[v]
            if x2 > x1: return 'E'
            if x2 < x1: return 'W'
            if y2 > y1: return 'N'
            if y2 < y1: return 'S'
            return 'N'

        # Relative turn logic dict {(current_heading, new_heading): 'Turn_Symbol'}
        turns = {
            ('N', 'N'): 'F', ('N', 'E'): 'R', ('N', 'W'): 'L', ('N', 'S'): 'U',
            ('S', 'S'): 'F', ('S', 'W'): 'R', ('S', 'E'): 'L', ('S', 'N'): 'U',
            ('E', 'E'): 'F', ('E', 'S'): 'R', ('E', 'N'): 'L', ('E', 'W'): 'U',
            ('W', 'W'): 'F', ('W', 'N'): 'R', ('W', 'S'): 'L', ('W', 'E'): 'U'
        }

        initial_dir = get_cardinal_dir(path[0], path[1])
        current_heading = initial_dir
        
        lrfu_sequence = ['F'] 
        
        for i in range(1, len(path) - 1):
            next_dir = get_cardinal_dir(path[i], path[i+1])
            move = turns[(current_heading, next_dir)]
            lrfu_sequence.append(move)
            # Add 'F' after L, U, or R (but not after F)
            if move in ['L', 'U', 'R']:
                lrfu_sequence.append('F')
            current_heading = next_dir 
            
        return lrfu_sequence

    def calculate_path(self):
        """Handles the button click to find and draw the path."""
        try:
            start_node = int(self.start_combo.get())
            end_node = int(self.end_combo.get())
        except ValueError:
            messagebox.showwarning("Warning", "Please select valid start and end nodes.")
            return

        try:
            # Shortest Path & Length
            path = nx.shortest_path(self.G, source=start_node, target=end_node, weight='weight')
            path_length = nx.shortest_path_length(self.G, source=start_node, target=end_node, weight='weight')
            
            # Generate LRFU commands
            lrfu_str = ' '.join(self.get_lrfu_sequence(path))
            
            info_text = (
                f"Mode: Point-to-Point\n"
                f"Distance: {path_length} units | Steps: {len(path)-1}\n"
                f"Path Nodes: {' → '.join(map(str, path))}\n"
                f"LRFU Sequence:\n{lrfu_str}"
            )
            self.info_label.config(text=info_text)
            self.draw_graph(path=path)
            
        except nx.NetworkXNoPath:
            self.info_label.config(text="No path found between these nodes.")
            messagebox.showinfo("No Path", f"No path exists between node {start_node} and node {end_node}.")

    def explore_map(self):
        """Finds the shortest path that visits every single node in the map."""
        try:
            start_node = int(self.start_combo.get())
        except ValueError:
            messagebox.showwarning("Warning", "Please select a valid start node.")
            return

        try:
            # 1. Use NetworkX TSP approximation to find the optimal walk visiting all nodes
            tsp_path = nx.approximation.traveling_salesman_problem(self.G, weight='weight', cycle=False)
            
            # 2. To ensure we start where the user requested, connect their Start Node 
            # to the closest end of the generated TSP path.
            dist_to_start = nx.shortest_path_length(self.G, start_node, tsp_path[0], weight='weight')
            dist_to_end = nx.shortest_path_length(self.G, start_node, tsp_path[-1], weight='weight')
            
            if dist_to_end < dist_to_start:
                tsp_path.reverse()
                
            prefix = nx.shortest_path(self.G, source=start_node, target=tsp_path[0], weight='weight')
            
            # Combine paths (slicing [:-1] avoids duplicating the connection node)
            final_path = prefix[:-1] + tsp_path
            
            # 3. Calculate total distance along this new route
            path_length = sum(self.G[u][v]['weight'] for u, v in zip(final_path, final_path[1:]))
            
            # 4. Generate LRFU commands
            lrfu_str = ' '.join(self.get_lrfu_sequence(final_path))
            
            info_text = (
                f"Mode: Full Map Exploration\n"
                f"Total Distance: {path_length} units | Steps: {len(final_path)-1}\n"
                f"Path Nodes: {' → '.join(map(str, final_path))}\n"
                f"LRFU Sequence:\n{lrfu_str}"
            )
            self.info_label.config(text=info_text)
            self.draw_graph(path=final_path)
            
        except Exception as e:
            messagebox.showerror("Error", f"Could not compute full map exploration:\n{e}")

    def reset_map(self):
        """Clears the path and resets the map visualization."""
        self.info_label.config(text="")
        self.draw_graph()

# ==========================================
# Helper Functions for External Use
# ==========================================
def get_path_and_commands(csv_path, start_node, end_node):
    """
    Generates the path and LRFU commands between two nodes.
    
    Args:
        csv_path (str): Path to the CSV file
        start_node (int): Starting node
        end_node (int): Ending node
    
    Returns:
        tuple: (path_list, lrfu_string)
    """
    try:
        G, pos = MapPathfinderUI.load_graph_data_static(csv_path)
        path = nx.shortest_path(G, source=start_node, target=end_node, weight='weight')
        
        # Create a temporary instance to use get_lrfu_sequence
        dummy_root = tk.Tk()
        dummy_root.withdraw()
        app = MapPathfinderUI(dummy_root, csv_path)
        lrfu_sequence = app.get_lrfu_sequence(path)
        lrfu_str = ''.join(lrfu_sequence)
        dummy_root.destroy()
        
        return path, lrfu_str
    except Exception as e:
        print(f"Error getting path: {e}")
        return None, None

def get_explore_map_commands(csv_path, start_node):
    """
    Generates the path and LRFU commands for full map exploration.
    
    Args:
        csv_path (str): Path to the CSV file
        start_node (int): Starting node
    
    Returns:
        tuple: (path_list, lrfu_string)
    """
    try:
        G, pos = MapPathfinderUI.load_graph_data_static(csv_path)
        
        # Use NetworkX TSP approximation
        tsp_path = nx.approximation.traveling_salesman_problem(G, weight='weight', cycle=False)
        
        # Ensure we start at the requested node
        dist_to_start = nx.shortest_path_length(G, start_node, tsp_path[0], weight='weight')
        dist_to_end = nx.shortest_path_length(G, start_node, tsp_path[-1], weight='weight')
        
        if dist_to_end < dist_to_start:
            tsp_path.reverse()
        
        prefix = nx.shortest_path(G, source=start_node, target=tsp_path[0], weight='weight')
        final_path = prefix[:-1] + tsp_path
        
        # Create a temporary instance to use get_lrfu_sequence
        dummy_root = tk.Tk()
        dummy_root.withdraw()
        app = MapPathfinderUI(dummy_root, csv_path)
        lrfu_sequence = app.get_lrfu_sequence(final_path)
        lrfu_str = ''.join(lrfu_sequence)
        dummy_root.destroy()
        
        return final_path, lrfu_str
    except Exception as e:
        print(f"Error exploring map: {e}")
        return None, None

# ==========================================
# Run the Application
# ==========================================
if __name__ == "__main__":
    # Ensure this matches your local environment path!
    CSV_FILENAME = '../map/maze (3).csv' 
    
    root = tk.Tk()
    app = MapPathfinderUI(root, CSV_FILENAME)
    root.mainloop()