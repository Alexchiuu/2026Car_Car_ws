import tkinter as tk
from tkinter import ttk, messagebox
import pandas as pd
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import traceback
import itertools

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
            self.start_node = None
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

    def get_leaf_nodes_with_scores(self):
        """
        Identifies leaf nodes (nodes with degree 1) and calculates their scores.
        Score = (x_distance + y_distance to starting point) * 25
        
        Returns:
            dict: {node: score}
        """
        leaf_nodes = {}
        
        # Use start_node if set, else default to node 1
        reference_node = self.start_node if self.start_node is not None else 1
        
        # Check if reference_node exists in the graph
        if reference_node not in self.G.nodes():
            return leaf_nodes
        
        reference_pos = self.pos[reference_node]
        
        for node in self.G.nodes():
            # A leaf node has degree 1 (only one connection)
            if self.G.degree(node) == 1:
                node_pos = self.pos[node]
                x_dist = abs(node_pos[0] - reference_pos[0])
                y_dist = abs(node_pos[1] - reference_pos[1])
                score = int((x_dist + y_dist) * 25)
                leaf_nodes[node] = score
        
        return leaf_nodes

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

        # Draw leaf node scores
        leaf_nodes_with_scores = self.get_leaf_nodes_with_scores()
        for node, score in leaf_nodes_with_scores.items():
            x, y = self.pos[node]
            self.ax.text(x, y - 0.5, f'S:{score}', fontsize=8, ha='center', 
                        bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.7))

        self.ax.axis('off')
        self.fig.tight_layout()
        self.canvas.draw()

    def get_lrfu_sequence(self, path, explore_mode=False):
        """Converts node sequence into a Left/Right/Forward/U-turn string.

        If explore_mode is True then U-turns where the right side of the node
        has no adjacent node in that right-direction will be encoded as 'V'.
        """
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

        # Right-direction vectors (relative to current heading)
        right_vectors = {'N': (1, 0), 'E': (0, -1), 'S': (-1, 0), 'W': (0, 1)}

        for i in range(1, len(path) - 1):
            next_dir = get_cardinal_dir(path[i], path[i+1])
            move = turns[(current_heading, next_dir)]

            move_char = move
            # If in exploration mode and the move is a U-turn, check the right-side
            if explore_mode and move == 'U':
                u = path[i]
                ux, uy = self.pos[u]
                dx, dy = right_vectors[current_heading]

                # Determine if any node exists on the right side (in that absolute direction)
                found_on_right = False
                tol = 1e-6
                for v, (vx, vy) in self.pos.items():
                    if v == u:
                        continue
                    # If right direction is horizontal
                    if dx != 0:
                        if abs(vy - uy) < tol and (vx - ux) * dx > 0:
                            found_on_right = True
                            break
                    else:
                        if abs(vx - ux) < tol and (vy - uy) * dy > 0:
                            found_on_right = True
                            break

                if not found_on_right:
                    # Right side outside map -> use 'V' instead of 'U'
                    move_char = 'V'

            lrfu_sequence.append(move_char)
            # Add 'F' after L, U, R or V (but not after F)
            if move_char in ['L', 'U', 'R', 'V']:
                lrfu_sequence.append('F')
            current_heading = next_dir

        return lrfu_sequence

    def calculate_path(self):
        """Handles the button click to find and draw the path."""
        try:
            start_node = int(self.start_combo.get())
            end_node = int(self.end_combo.get())
            self.start_node = start_node
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

    def calculate_lrfu_time(self, lrfu_sequence):
        """
        Calculate the time taken for a LRFU sequence.
        F: 1 second, L/R: 1.5 seconds, U/V: 2 seconds
        """
        time_map = {'F': 1, 'L': 1.5, 'R': 1.5, 'U': 2, 'V': 2}
        return sum(time_map.get(c, 0) for c in lrfu_sequence)

    def calculate_path_score(self, path):
        """
        Calculate total score for a path by summing scores of all leaf nodes visited.
        """
        leaf_scores = self.get_leaf_nodes_with_scores()
        return sum(leaf_scores.get(node, 0) for node in set(path))

    def find_best_path_with_time_limit(self, start_node, time_limit=70):
        """
        Find the path starting from start_node that maximizes leaf node scores
        within the given time limit (in seconds).
        Uses a Greedy Algorithm approach to prevent exponential UI freezing.
        
        Returns:
            tuple: (best_path, best_lrfu_str, total_score, total_time)
        """
        leaf_scores = self.get_leaf_nodes_with_scores()
        unvisited_leaves = set(leaf_scores.keys())
        
        total_score = 0
        if start_node in unvisited_leaves:
            total_score += leaf_scores[start_node]
            unvisited_leaves.remove(start_node)
            
        current_path = [start_node]
        current_time = 0
        current_lrfu = []
        
        while unvisited_leaves:
            over_time = False
            best_next_leaf = None
            best_metric = -1
            best_path_extension = []
            best_lrfu_extension = []
            best_time_total = current_time
            
            if len(unvisited_leaves) >= 4:
                # Consider quartets
                best_quartet = None
                best_quartet_score = -1
                best_quartet_path = []
                best_quartet_lrfu = []
                best_quartet_time = 0
                for quartet in itertools.combinations(unvisited_leaves, 4):
                    for perm in itertools.permutations(quartet):
                        try:
                            # Compute path from current to perm[0], then to perm[1], ..., then to perm[3]
                            sp0 = nx.shortest_path(self.G, source=current_path[-1], target=perm[0], weight='weight')
                            sp1 = nx.shortest_path(self.G, source=perm[0], target=perm[1], weight='weight')
                            sp2 = nx.shortest_path(self.G, source=perm[1], target=perm[2], weight='weight')
                            sp3 = nx.shortest_path(self.G, source=perm[2], target=perm[3], weight='weight')
                            full_path = current_path + sp0[1:] + sp1[1:] + sp2[1:] + sp3[1:]
                            full_lrfu = self.get_lrfu_sequence(full_path, explore_mode=True)
                            full_time = self.calculate_lrfu_time(full_lrfu)
                            if full_time <= time_limit:
                                quartet_score = sum(leaf_scores[leaf] for leaf in perm)
                                if quartet_score > best_quartet_score:
                                    best_quartet_score = quartet_score
                                    best_quartet = perm
                                    best_quartet_path = sp0[1:] + sp1[1:] + sp2[1:] + sp3[1:]
                                    best_quartet_lrfu = full_lrfu
                                    best_quartet_time = full_time
                        except nx.NetworkXNoPath:
                            continue
                if best_quartet:
                    # Add the four
                    current_path.extend(best_quartet_path)
                    current_lrfu = best_quartet_lrfu
                    current_time = best_quartet_time
                    total_score += best_quartet_score
                    for leaf in best_quartet:
                        unvisited_leaves.remove(leaf)
                    continue  # Skip the single leaf logic
            
            # Fallback to single leaf logic
            for leaf in unvisited_leaves:
                try:
                    # Find shortest path to this specific leaf node
                    sp = nx.shortest_path(self.G, source=current_path[-1], target=leaf, weight='weight')
                    
                    if len(sp) > 1:
                        test_path = current_path + sp[1:]
                        test_lrfu = self.get_lrfu_sequence(test_path, explore_mode=True)
                        test_time = self.calculate_lrfu_time(test_lrfu)
                        
                        if test_time <= time_limit:
                            time_added = test_time - current_time
                            if time_added <= 0: 
                                time_added = 0.1 # Prevent division by zero
                            
                            # Heuristic metric: score per second spent getting there
                            metric = leaf_scores[leaf] / time_added
                            
                            if metric > best_metric:
                                best_metric = metric
                                best_next_leaf = leaf
                                best_path_extension = sp[1:]
                                best_lrfu_extension = test_lrfu
                                best_time_total = test_time
                except nx.NetworkXNoPath:
                    continue
                    
            if best_next_leaf is None:
                # No remaining leaves can be reached within the strict time limit
                # But allow going over time for one more node: select the leaf with the shortest path
                if unvisited_leaves:
                    min_dist = float('inf')
                    best_next_leaf = None
                    for leaf in unvisited_leaves:
                        try:
                            dist = nx.shortest_path_length(self.G, source=current_path[-1], target=leaf, weight='weight')
                            if dist < min_dist:
                                min_dist = dist
                                best_next_leaf = leaf
                        except nx.NetworkXNoPath:
                            continue
                    if best_next_leaf is not None:
                        sp = nx.shortest_path(self.G, source=current_path[-1], target=best_next_leaf, weight='weight')
                        if len(sp) > 1:
                            best_path_extension = sp[1:]
                            test_path = current_path + best_path_extension
                            best_lrfu_extension = self.get_lrfu_sequence(test_path, explore_mode=True)
                            best_time_total = self.calculate_lrfu_time(best_lrfu_extension)
                            over_time = True  # Mark that this is over time
                            # Proceed even if over time
                        else:
                            break  # Should not happen
                    else:
                        break  # No reachable leaves
                else:
                    break
                
            # Commit to the best leaf calculated
            current_path.extend(best_path_extension)
            current_lrfu = best_lrfu_extension
            current_time = best_time_total
            total_score += leaf_scores[best_next_leaf]
            unvisited_leaves.remove(best_next_leaf)
            
            if over_time:
                break  # Stop after adding the one over-time leaf
            
        best_lrfu_str = ''.join(current_lrfu) if current_lrfu else ""
        
        # Fallback if the path couldn't be extended but contains multiple nodes
        if not best_lrfu_str and len(current_path) >= 2:
            best_lrfu_str = ''.join(self.get_lrfu_sequence(current_path, explore_mode=True))
            
        return current_path, best_lrfu_str, total_score, current_time

    def explore_map(self):
        """Finds the path with highest leaf node score within 70 second time constraint."""
        try:
            start_node = int(self.start_combo.get())
            self.start_node = start_node
        except ValueError:
            messagebox.showwarning("Warning", "Please select a valid start node.")
            return

        try:
            time_limit = 70  # 20 seconds
            final_path, lrfu_str, total_score, total_time = self.find_best_path_with_time_limit(start_node, time_limit)
            
            # Calculate total distance along this route
            path_length = sum(self.G[u][v]['weight'] for u, v in zip(final_path, final_path[1:]))
            
            info_text = (
                f"Mode: Time-Limited Exploration (70s limit)\n"
                f"Total Distance: {path_length} units | Steps: {len(final_path)-1}\n"
                f"Total Score: {total_score} points | Time Used: {total_time:.1f}s\n"
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
        self.start_node = None
        self.draw_graph()

    def get_next_explore_commands(csv_path, current_node, remaining_time, max_commands=5, current_heading=None):
        """
        Generates the next path segment and LRFU commands for exploration from current_node within remaining_time,
        adjusting for the current heading.
        
        Args:
            csv_path (str): Path to the CSV file
            current_node (int): Current node
            remaining_time (float): Remaining time in seconds
            max_commands (int): Maximum number of commands to return
            current_heading (str): Current heading ('N', 'E', 'S', 'W') or None
        
        Returns:
            tuple: (path_segment, commands_str, ending_heading)
        """
        try:
            # Create a temporary instance
            dummy_root = tk.Tk()
            dummy_root.withdraw()
            app = MapPathfinderUI(dummy_root, csv_path)
            final_path, lrfu_str, _, _ = app.find_best_path_with_time_limit(current_node, remaining_time)
            dummy_root.destroy()
            
            if not lrfu_str or len(final_path) < 2:
                return None, None, None
            
            required_initial_heading = app.get_cardinal_dir(final_path[0], final_path[1])
            
            if current_heading and current_heading != required_initial_heading:
                turn_cmd = app.turns[(current_heading, required_initial_heading)]
                lrfu_str = turn_cmd + lrfu_str
            
            # Simulate heading to find ending_heading
            current_h = required_initial_heading
            for cmd in lrfu_str:
                if cmd == 'F':
                    pass
                elif cmd in ['L', 'R', 'U']:
                    for (from_h, to_h), turn in app.turns.items():
                        if from_h == current_h and turn == cmd:
                            current_h = to_h
                            break
        
            ending_heading = current_h
            
            commands = lrfu_str[:max_commands]
            num_F = commands.count('F')
            path_segment = final_path[:num_F + 1]
            
            return path_segment, commands, ending_heading
        except Exception as e:
            print(f"Error getting next commands: {e}")
            return None, None, None

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

        # Validate that requested nodes exist in the graph and are integers
        if start_node not in G.nodes():
            raise ValueError(f"Start node {start_node} is not in graph. Available nodes: {list(G.nodes())}")
        if end_node not in G.nodes():
            raise ValueError(f"End node {end_node} is not in graph. Available nodes: {list(G.nodes())}")

        path = nx.shortest_path(G, source=start_node, target=end_node, weight='weight')
        
        # Create a temporary instance to use get_lrfu_sequence
        # Use a hidden Tk root to avoid showing the UI when called from headless scripts
        dummy_root = tk.Tk()
        dummy_root.withdraw()
        app = MapPathfinderUI(dummy_root, csv_path)
        lrfu_sequence = app.get_lrfu_sequence(path)
        lrfu_str = ''.join(lrfu_sequence)
        dummy_root.destroy()
        
        return path, lrfu_str
    except Exception as e:
        # Print full traceback to help debug issues like missing nodes or CSV parse problems
        traceback.print_exc()
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
        lrfu_sequence = app.get_lrfu_sequence(final_path, explore_mode=True)
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
    CSV_FILENAME = '../map/big_maze_114.csv' 

    root = tk.Tk()
    app = MapPathfinderUI(root, CSV_FILENAME)
    root.mainloop()