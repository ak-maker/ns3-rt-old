import time
import tensorflow as tf
import numpy as np
import socket
from sionna.rt import load_scene, PlanarArray, Transmitter, Receiver, Paths
from sionna.constants import SPEED_OF_LIGHT
import os, subprocess, signal
import argparse


file_name = "scenarios/SionnaExampleScenario/scene.xml"
#local_machine = True
#verbose = False

def manage_location_message(message, sionna_structure):
    try:
        # Handle map_update message
        data = message[len("LOC_UPDATE:"):] # message: 'LOC_UPDATE:obj3,0.000000,0.000000,1.500000,0.000000'
        parts = data.split(",") # data: 'obj3,0.000000,0.000000,1.500000,0.000000'
        car = int(parts[0].replace("obj", "")) # car: 3

        new_x = float(parts[1])
        new_y = float(parts[2])
        new_z = float(parts[3]) + 1
        new_angle = float(parts[4])

        # Save in SUMO_live_location_db
        # {3: {'x': 0.0, 'y': 0.0, 'z': 2.5, 'angle': 0.0}}
        sionna_structure["SUMO_live_location_db"][car] = {"x": new_x, "y": new_y, "z": new_z, "angle": new_angle}

        # Check if the vehicle exists in Sionna_location_db
        if car in sionna_structure["sionna_location_db"]: # 第一次 sionna_structure["sionna_location_db"]是{}
            # Fetch the old position and angle
            old_x = sionna_structure["sionna_location_db"][car]["x"]
            old_y = sionna_structure["sionna_location_db"][car]["y"]
            old_z = sionna_structure["sionna_location_db"][car]["z"]
            old_angle = sionna_structure["sionna_location_db"][car]["angle"]

            # Check if the position or angle has changed by more than the thresholds
            position_changed = (
                    abs(new_x - old_x) >= sionna_structure["position_threshold"]
                    or abs(new_y - old_y) >= sionna_structure["position_threshold"]
                    or abs(new_z - old_z) >= sionna_structure["position_threshold"]
            )
            angle_changed = abs(new_angle - old_angle) >= sionna_structure["angle_threshold"]
        else:
            # No previous record, so this is the first update (considered a change)
            position_changed = True
            angle_changed = True

        # If the position or angle has changed, update the dictionary and the scene
        if position_changed or angle_changed:
            # Update Sionna_location_db with the new values
            sionna_structure["sionna_location_db"][car] = sionna_structure["SUMO_live_location_db"][car] # {3: {'x': 0.0, 'y': 0.0, 'z': 2.5, 'angle': 0.0}}
            # Clear the path_loss cache as one of the car's position has changed (must do for vNLOS cases)
            sionna_structure["path_loss_cache"] = {}
            sionna_structure["rays_cache"] = {}
            # print("Pathloss cache cleared.")
            # Print the updated car's information for logging
            if sionna_structure["verbose"]:
                print(f"car_{car} - Position: [{new_x}, {new_y}, {new_z}] - Angle: {new_angle}")
            # Apply changes to the scene
            if sionna_structure["scene"].get(f"car_{car}"):  # Make sure the object exists in the scene
                from_sionna = sionna_structure["scene"].get(f"car_{car}")
                from_sionna.position = [new_x, new_y, new_z]
                # Orientation is not changed because of a SIONNA bug (kernel crashes)
                # new_orientation = (new_angle*np.pi/180, 0, 0)
                # from_sionna.orientation = type(from_sionna.orientation)(new_orientation, device=from_sionna.orientation.device)

                if sionna_structure["verbose"]:
                    print(f"Updated car_{car} position in the scene.")
            else:
                print(f"ERROR: no car_{car} in the scene, use Blender to check")

            sionna_structure["scene"].remove(f"car_{car}_tx_antenna")
            sionna_structure["scene"].remove(f"car_{car}_rx_antenna")
        return car

    except (IndexError, ValueError) as e:
        print(f"EXCEPTION - Location parsing failed: {e}")
        return None


def match_rays_to_cars(paths, sionna_structure):
    matched_paths = {}
    targets = paths.targets.numpy()
    sources = paths.sources.numpy()

    # paths_mask_np = paths.mask.numpy()
    path_coefficients_np = paths.a.numpy() # a 里保存的每个元素（通常是一个复数）就代表了对应路径到达接收端的振幅与相位信息。在进行信道合成或后续信号处理时，往往需要将所有路径的 a 做合并(矢量相加)来得到最终的接收信号。
    delays_np = paths.tau.numpy() # tau 通常表示每条射线路径的时延（time delay)
    types_np = paths.types.numpy() # 用于标识每条路径属于哪种传播机制或组合类型 # 通常会给这些类型赋予枚举值或整数索引（比如 0 表示 LOS，1 表示单次反射，2 表示散射，3 表示衍射 等等）
    paths_mask_np = paths.mask.numpy() # 指示每条路径是否“有效”或者需要被后续处理. True 表示该索引对应的那条路径是有效路径，应该被保留或进一步分析。False 表示该路径无效（可能因为它超出了最大追踪次数、传播损耗过大、时延太长等）。

    '''
    # Currently unused, may be useful for future work: commented for performance
    theta_t_np = paths.theta_t.numpy()
    phi_t_np = paths.phi_t.numpy()
    theta_r_np = paths.theta_r.numpy()
    phi_r_np = paths.phi_r.numpy()
    doppler_np = paths.doppler.numpy()
    '''

    # Pre-adjust car locations with antenna displacement
    adjusted_car_locs = {
        car_id: {"x": car_loc["x"] + sionna_structure["antenna_displacement"][0], "y": car_loc["y"] + sionna_structure["antenna_displacement"][1],
                 "z": car_loc["z"] + sionna_structure["antenna_displacement"][2]}
        for car_id, car_loc in sionna_structure["sionna_location_db"].items()
    }
    car_ids = np.array(list(adjusted_car_locs.keys()))
    car_positions = np.array([[loc["x"], loc["y"], loc["z"]] for loc in adjusted_car_locs.values()])

    # Iterate over each source (TX)
    for tx_idx, source in enumerate(sources):
        distances = np.linalg.norm(car_positions - source, axis=1) # [ 0.         58.86616259]
        source_within_tolerance = distances <= sionna_structure["position_threshold"] # [ True False]

        if np.any(source_within_tolerance): # 检查给定数组（或列表）中是否存在至少一个为 True。如果有，就返回 True；否则返回 False。
            min_idx = np.argmin(distances[source_within_tolerance]) # 0
            source_closest_car_id = car_ids[source_within_tolerance][min_idx] # 找到所有在容差范围内的车辆 ID，然后从这些车里取一个（通常是距离最近的那辆车）的 ID # 3
            matched_source_car_name = f"car_{source_closest_car_id}"

            if matched_source_car_name not in matched_paths:
                matched_paths[matched_source_car_name] = {}

            # Iterate over targets for the current source (TX)
            for rx_idx, target in enumerate(targets):
                if rx_idx >= paths.mask.shape[1]:
                    continue
                distances = np.linalg.norm(car_positions - target, axis=1)
                target_within_tolerance = distances <= sionna_structure["position_threshold"]

                if np.any(target_within_tolerance):
                    min_idx = np.argmin(distances[target_within_tolerance])
                    target_closest_car_id = car_ids[target_within_tolerance][min_idx]
                    matched_target_car_name = f"car_{target_closest_car_id}"

                    if matched_target_car_name not in matched_paths[matched_source_car_name]:
                        matched_paths[matched_source_car_name][matched_target_car_name] = {
                            # 'paths_mask': [],
                            'path_coefficients': [],
                            'delays': [],
                            # 'angles_of_departure': {'zenith': [], 'azimuth': []},
                            # 'angles_of_arrival': {'zenith': [], 'azimuth': []},
                            # 'doppler': [],
                            'is_los': []
                        }

                    # Populate path data
                    try:
                        matched_paths[matched_source_car_name][matched_target_car_name]['path_coefficients'].append(
                            path_coefficients_np[0, rx_idx, 0, tx_idx, 0, ...]) # (1,2,1,2,1,25,1) 取(0,0,0,0,0) tx=0 rx=0
                        matched_paths[matched_source_car_name][matched_target_car_name]['delays'].append(
                            delays_np[0, rx_idx, tx_idx, ...])

                        '''
                        # Currently unused, may be useful for future work: commented for performance
                        matched_paths[matched_source_car_name][matched_target_car_name]['angles_of_departure']['zenith'].append(theta_t_np[0, rx_idx, tx_idx, ...])
                        matched_paths[matched_source_car_name][matched_target_car_name]['angles_of_departure']['azimuth'].append(phi_t_np[0, rx_idx, tx_idx, ...])
                        matched_paths[matched_source_car_name][matched_target_car_name]['angles_of_arrival']['zenith'].append(theta_r_np[0, rx_idx, tx_idx, ...])
                        matched_paths[matched_source_car_name][matched_target_car_name]['angles_of_arrival']['azimuth'].append(phi_r_np[0, rx_idx, tx_idx, ...])
                        matched_paths[matched_source_car_name][matched_target_car_name]['doppler'].append(doppler_np[0, rx_idx, tx_idx, ...])
                        '''

                        # Extract LoS determination
                        valid_paths_mask = paths_mask_np[0, rx_idx, tx_idx, :]
                        valid_path_indices = np.where(valid_paths_mask)[0] # np.where(valid_paths_mask) 会返回一个元组，其中每个元素都是满足 valid_paths_mask == True 条件的位置索引。在这个元组的第 0 个元素处，就是这些索引值组成的那条数组。
                        valid_path_types = types_np[0][valid_path_indices]
                        # Check if any path is LoS
                        is_los = np.any(valid_path_types == Paths.LOS)
                        matched_paths[matched_source_car_name][matched_target_car_name]['is_los'].append(bool(is_los))

                    except (IndexError, tf.errors.InvalidArgumentError) as e:
                        print(f"Error encountered for source {tx_idx}, target {rx_idx}: {e}")
                        continue
                else:
                    if sionna_structure["verbose"]:
                        print(f"Warning - No car within tolerance for target {rx_idx} (for source {tx_idx})")
        else:
            if sionna_structure["verbose"]:
                print(f"Warning - No car within tolerance for source {tx_idx}")

    return matched_paths


def compute_rays(sionna_structure):
    t = time.time()

    sionna_structure["scene"].tx_array = sionna_structure["planar_array"] # transmitter
    sionna_structure["scene"].rx_array = sionna_structure["planar_array"] # receiver

    # Ensure every car in the simulation has antennas (one for TX and one for RX)
    for car_id in sionna_structure["sionna_location_db"]:
        tx_antenna_name = f"car_{car_id}_tx_antenna"
        rx_antenna_name = f"car_{car_id}_rx_antenna"
        car_position = np.array(
            [sionna_structure["sionna_location_db"][car_id]['x'], sionna_structure["sionna_location_db"][car_id]['y'],
             sionna_structure["sionna_location_db"][car_id]['z']])
        tx_position = car_position + np.array(sionna_structure["antenna_displacement"]) # eg: [0, 0, 4]
        rx_position = car_position + np.array(sionna_structure["antenna_displacement"])

        if sionna_structure["scene"].get(tx_antenna_name) is None:
            sionna_structure["scene"].add(Transmitter(tx_antenna_name, position=tx_position, orientation=[0, 0, 0])) # scene.transmitter
            sionna_structure["scene"].tx_array = sionna_structure["scene"].tx_array # ?
            if sionna_structure["verbose"]:
                print(f"Added TX antenna for car_{car_id}: {tx_antenna_name}")

        if sionna_structure["scene"].get(rx_antenna_name) is None:
            sionna_structure["scene"].add(Receiver(rx_antenna_name, position=rx_position, orientation=[0, 0, 0]))
            sionna_structure["scene"].rx_array = sionna_structure["scene"].rx_array
            if sionna_structure["verbose"]:
                print(f"Added RX antenna for car_{car_id}: {rx_antenna_name}")

    # Compute paths
    paths = sionna_structure["scene"].compute_paths(max_depth=sionna_structure["max_depth"],
                                                    num_samples=sionna_structure["num_samples"], diffraction=True,
                                                    scattering=True)
    paths.normalize_delays = False  # Do not normalize delays to the first path
    if sionna_structure["verbose"]:
        print(f"Ray tracing took: {(time.time() - t) * 1000} ms")
    t = time.time()
    matched_paths = match_rays_to_cars(paths, sionna_structure)
    if sionna_structure["verbose"]:
        print(f"Matching rays to cars took: {(time.time() - t) * 1000} ms")

    # Iterate over sources in matched_paths
    for src_car_id in sionna_structure["sionna_location_db"]:
        current_source_car_name = f"car_{src_car_id}"
        if current_source_car_name in matched_paths:
            matched_paths_for_source = matched_paths[current_source_car_name]

            # Iterate over targets for the current source
            for trg_car_id in sionna_structure["sionna_location_db"]:
                current_target_car_name = f"car_{trg_car_id}"
                if current_target_car_name != current_source_car_name:  # Skip case where source == target
                    if current_target_car_name in matched_paths_for_source:
                        if current_source_car_name not in sionna_structure["rays_cache"]:
                            sionna_structure["rays_cache"][current_source_car_name] = {}
                        # Cache the matched paths for this source-target pair
                        sionna_structure["rays_cache"][current_source_car_name][current_target_car_name] = \
                            matched_paths_for_source[current_target_car_name]
                        if sionna_structure["verbose"]:
                            print(
                                f"Cached paths for source {current_source_car_name} to target {current_target_car_name}")
                    else:
                        # Force an update if the source or target wasn't matched
                        for car_id in sionna_structure["sionna_location_db"]:
                            car_name = f"car_{car_id}"
                            if sionna_structure["scene"].get(car_name):
                                from_sionna = sionna_structure["scene"].get(car_name)
                                new_position = [sionna_structure["SUMO_live_location_db"][car_id]["x"],
                                                sionna_structure["SUMO_live_location_db"][car_id]["y"],
                                                sionna_structure["SUMO_live_location_db"][car_id]["z"]]
                                from_sionna.position = new_position
                                # Update Sionna location database with new positions
                                sionna_structure["sionna_location_db"][car_id] = {"x": new_position[0],
                                                                                  "y": new_position[1],
                                                                                  "z": new_position[2], "angle":
                                                                                      sionna_structure[
                                                                                          "SUMO_live_location_db"][
                                                                                          car_id]["angle"]}
                                # Update antenna positions
                                if sionna_structure["scene"].get(f"{car_name}_tx_antenna"):
                                    sionna_structure["scene"].get(f"{car_name}_tx_antenna").position = \
                                        [new_position[0] + sionna_structure["antenna_displacement"][0],
                                         new_position[1] + sionna_structure["antenna_displacement"][1],
                                         new_position[2] + sionna_structure["antenna_displacement"][2]]
                                    if sionna_structure["verbose"]:
                                        print(f"Forced update for {car_name} and its TX antenna in the scene.")
                                if sionna_structure["scene"].get(f"{car_name}_rx_antenna"):
                                    sionna_structure["scene"].get(f"{car_name}_rx_antenna").position = \
                                        [new_position[0] + sionna_structure["antenna_displacement"][0],
                                         new_position[1] + sionna_structure["antenna_displacement"][1],
                                         new_position[2] + sionna_structure["antenna_displacement"][2]]
                                    if sionna_structure["verbose"]:
                                        print(f"Forced update for {car_name} and its RX antenna in the scene.")
                            else:
                                print(f"ERROR: no {car_name} in the scene for forced update, use Blender to check")

                        # Re-do matching with updated locations
                        t = time.time()
                        matched_paths = match_rays_to_cars(paths, sionna_structure)
                        if sionna_structure["verbose"]:
                            print(f"Matching rays to cars (double exec) took: {(time.time() - t) * 1000} ms")
                        if current_source_car_name not in sionna_structure["rays_cache"]:
                            sionna_structure["rays_cache"][current_source_car_name] = {}
                        if current_target_car_name in matched_paths[current_source_car_name]:
                            sionna_structure["rays_cache"][current_source_car_name][current_target_car_name] = \
                                matched_paths[current_source_car_name][current_target_car_name]

    return None


def get_path_loss(car1_id, car2_id, sionna_structure):
    # Was the requested value already calculated?
    if car1_id not in sionna_structure["rays_cache"] or car2_id not in sionna_structure["rays_cache"][car1_id]:
        compute_rays(sionna_structure)

    path_coefficients = sionna_structure["rays_cache"][car1_id][car2_id]["path_coefficients"]
    sum = np.sum(path_coefficients) # (7.012584e-05-1.3132263e-06j)
    abs = np.abs(sum)
    square = abs ** 2
    total_cir = square

    # Calculate path loss in dB
    if total_cir > 0:
        path_loss = -10 * np.log10(total_cir)
    else:
        # Handle the case where path loss calculation is not valid
        if sionna_structure["verbose"]:
            print(
                f"Pathloss calculation failed for {car1_id}-{car2_id}: got infinite value (not enough rays). Returning 300 dB.")
        path_loss = 300  # Assign 300 dB for loss cases

    return path_loss


def manage_path_loss_request(message, sionna_structure):
    try:
        data = message[len("CALC_REQUEST_PATHGAIN:"):] # 首次： message: 'CALC_REQUEST_PATHGAIN:obj3,obj4'
        parts = data.split(",")
        car_a_str = parts[0].replace("obj", "")
        car_b_str = parts[1].replace("obj", "")

        # Getting each car_id, the origin is marked as 0
        car_a_id = "origin" if car_a_str == "0" else f"car_{int(car_a_str)}" if car_a_str else "origin" # car_3
        car_b_id = "origin" if car_b_str == "0" else f"car_{int(car_b_str)}" if car_b_str else "origin"

        if car_a_id == "origin" or car_b_id == "origin":
            # If any, ignoring path_loss requests from the origin, used for statistical calibration
            path_loss_value = 0
        else:
            t = time.time()
            path_loss_value = get_path_loss(car_a_id, car_b_id, sionna_structure)

        return path_loss_value

    except (ValueError, IndexError) as e:
        print(f"EXCEPTION - Error processing path_loss request: {e}")
        return None


def get_delay(car1_id, car2_id, sionna_structure):
    # Check and compute rays only if necessary
    if car1_id not in sionna_structure["rays_cache"] or car2_id not in sionna_structure["rays_cache"][car1_id]:
        compute_rays(sionna_structure)

    delays = np.abs(sionna_structure["rays_cache"][car1_id][car2_id]["delays"])
    delays_flat = delays.flatten()

    # Filter positive values
    positive_values = delays_flat[delays_flat >= 0]

    if positive_values.size > 0:
        min_positive_value = np.min(positive_values)
    else:
        min_positive_value = 1e5

    return min_positive_value


def manage_delay_request(message, sionna_structure):
    try:
        data = message[len("CALC_REQUEST_DELAY:"):]
        parts = data.split(",")
        car_a_str = parts[0].replace("obj", "")
        car_b_str = parts[1].replace("obj", "")

        # Getting each car_id, the origin is marked as 0
        car_a_id = "origin" if car_a_str == "0" else f"car_{int(car_a_str)}" if car_a_str else "origin"
        car_b_id = "origin" if car_b_str == "0" else f"car_{int(car_b_str)}" if car_b_str else "origin"

        if car_a_id == "origin" or car_b_id == "origin":
            # If any, ignoring path_loss requests from the origin, used for statistical calibration
            delay = 0
        else:
            delay = get_delay(car_a_id, car_b_id, sionna_structure)

        return delay

    except (ValueError, IndexError) as e:
        print(f"EXCEPTION - Error processing delay request: {e}")
        return None


def manage_los_request(message, sionna_structure):
    try:
        data = message[len("CALC_REQUEST_LOS:"):]
        parts = data.split(",")
        car_a_str = parts[0].replace("obj", "")
        car_b_str = parts[1].replace("obj", "")

        # Getting each car_id, the origin is marked as 0
        car_a_id = "origin" if car_a_str == "0" else f"car_{int(car_a_str)}" if car_a_str else "origin"
        car_b_id = "origin" if car_b_str == "0" else f"car_{int(car_b_str)}" if car_b_str else "origin"

        if car_a_id == "origin" or car_b_id == "origin":
            # If any, ignoring path_loss requests from the origin, used for statistical calibration
            los = 0
        else:
            los = sionna_structure["rays_cache"][car_a_id][car_b_id]["is_los"]

        return los

    except (ValueError, IndexError) as e:
        print(f"EXCEPTION - Error processing LOS request: {e}")
        return None


# Function to kill processes using a specific port
def kill_process_using_port(port, verbose=False):
    try:
        result = subprocess.run(['lsof', '-i', f':{port}'], stdout=subprocess.PIPE)
        for line in result.stdout.decode('utf-8').split('\n'):
            if 'LISTEN' in line:
                pid = int(line.split()[1])
                os.kill(pid, signal.SIGKILL)
                if verbose:
                    print(f"Killed process {pid} using port {port}")
    except Exception as e:
        print(f"Error killing process using port {port}: {e}")


# Configure GPU settings
def configure_gpu(verbose=False):
    if os.getenv("CUDA_VISIBLE_DEVICES") is None:
        gpu_num = 2  # Default GPU setting
        os.environ["CUDA_VISIBLE_DEVICES"] = f"{gpu_num}"
    os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'

    gpus = tf.config.list_physical_devices('GPU')
    if gpus:
        try:
            for gpu in gpus:
                tf.config.experimental.set_memory_growth(gpu, True)
        except RuntimeError as e:
            print(e)

    tf.get_logger().setLevel('ERROR')
    if verbose:
        print("Configured TensorFlow and GPU settings.")


# Main function to manage initialization and variables
def main():
    # Argument parser setup
    parser = argparse.ArgumentParser(description='ns3-rt - Sionna Server Script: use the following options to configure the server.')
    parser.add_argument('--path-to-xml-scenario', type=str, default='scenarios/SionnaExampleScenario/scene.xml',
                        help='Path to the .xml file of the scenario (see Sionna documentation for the creation of custom scenarios)')
    parser.add_argument('--local-machine', action='store_true',
                        help='Flag to indicate if Sionna and ns3-rt are running on the same machine (locally)')
    parser.add_argument('--verbose', action='store_true', help='Flag for verbose output')
    parser.add_argument('--frequency', type=float, help='Frequency of the simulation in Hz', default=5.89e9)
    args = parser.parse_args()
    file_name = args.path_to_xml_scenario
    local_machine = args.local_machine
    verbose = args.verbose
    frequency = args.frequency

    # Kill any process using the port
    kill_process_using_port(8103, verbose)

    # Configure GPU
    configure_gpu(verbose)

    sionna_structure = dict()

    sionna_structure["verbose"] = verbose

    # Load scene and configure radio settings
    sionna_structure["scene"] = load_scene(file_name)
    sionna_structure["scene"].frequency = frequency  # Frequency in Hz
    # 更通俗点说，如果你只有一个（或少量）天线，但想要模拟一个大规模天线阵列的效果，
    # 可以通过在不同时刻、不同位置对信道（或信号）进行采样，然后将这些采样数据组合起来，形成一个等效的“大”阵列。
    # Sionna 中的 synthetic_array = True 就是在告诉系统使用这类合成阵列的处理流程。
    sionna_structure["scene"].synthetic_array = True  # Enable synthetic array processing
    # 这行代码表示将阵元（天线单元）之间的间隔 dd 设置为对应于当前频率下波长的一半（λ/2λ/2）
    # 在天线阵列设计中，半波长间距是一种常用且经典的选择，可以避免出现较严重的栅瓣（Grating Lobes），
    # 同时在方向图和增益等方面兼顾性能和实现复杂度。
    element_spacing = SPEED_OF_LIGHT / sionna_structure["scene"].frequency / 2
    # PlanarArray(
    #     1,                    # 行数（rows）
    #     1,                    # 列数（columns）# 表示此处仅定义了一个 1×1 的平面阵列，也就是只有一个天线单元。
    #     element_spacing,      # 行方向（或垂直方向）的阵元间隔
    #     element_spacing,      # 列方向（或水平方向）的阵元间隔
    #     "iso",                # 天线元素类型，"iso" 表示各向同性（isotropic）
    #     "V"                   # 极化方式，"V" 表示垂直极化
    # )
    # 综上所述，这行代码就是向 Sionna 声明了一个仅包含单个各向同性、垂直极化天线单元的平面阵列对象
    sionna_structure["planar_array"] = PlanarArray(1, 1, element_spacing, element_spacing, "iso", "V")
    # 设置天线在三维坐标中的偏移位置
    sionna_structure["antenna_displacement"] = [0, 0, 1.5]
    # 一般用于在场景建模或用户位置相关的逻辑中设定一个距离阈值，单位通常是米。
    # 例如，当用户或目标对象与参考点（或天线、基站等）之间的距离小于这个阈值时，
    # 系统会执行某些特定的处理逻辑（如切换模型、触发事件、忽略/剔除过近的点位等）。
    sionna_structure["position_threshold"] = 3  # Position update threshold in meters
    sionna_structure["angle_threshold"] = 90  # Angle update threshold in degrees
    # 表示在进行射线追踪（ray tracing）时所允许的最大反射/散射次数。也就是说，信号光线可以在场景中经过最多 5 次反射
    # （或者包含其他类型的散射、衍射等事件），超过这个次数就停止继续追踪该条光线。这是为了在仿真中平衡计算精度和计算开销
    sionna_structure["max_depth"] = 5  # Maximum ray tracing depth
    # 会进行 10000 个不同状态或位置（即“样本”）的仿真
    sionna_structure["num_samples"] = 1e4  # Number of samples for ray tracing

    sionna_structure["path_loss_cache"] = {}
    sionna_structure["delay_cache"] = {}
    sionna_structure["last_path_loss_requested"] = None

    # Set up UDP socket
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if local_machine:
        udp_socket.bind(("127.0.0.1", 8103))  # Local machine configuration
    else:
        udp_socket.bind(("0.0.0.0", 8103))  # External server configuration

    # Databases for vehicle locations
    sionna_structure["SUMO_live_location_db"] = {}  # Real-time vehicle locations in SUMO
    sionna_structure["sionna_location_db"] = {}  # Vehicle locations in Sionna

    sionna_structure["rays_cache"] = {}  # Cache for ray information
    sionna_structure["path_loss_cache"] = {}  # Cache for path loss values

    # Simulation main loop or function calls could go here
    # Example:
    # process_location_updates(scene, SUMO_live_location_db, Sionna_location_db, ...)
    # manage_requests(udp_socket, rays_cache, ...)

    print(f"Simulation setup complete. Ready to process requests. Ray Tracing is working at {frequency / 1e9} GHz.")

    while True:
        # Receive data from the socket
        payload, address = udp_socket.recvfrom(1024)
        message = payload.decode()

        if message.startswith("LOC_UPDATE:"):
            updated_car = manage_location_message(message, sionna_structure)
            if updated_car is not None:
                response = "LOC_CONFIRM:" + "obj" + str(updated_car) # response: 'LOC_CONFIRM:obj3'
                udp_socket.sendto(response.encode(), address)

        if message.startswith("CALC_REQUEST_PATHGAIN:"):
            pathloss = manage_path_loss_request(message, sionna_structure)
            if pathloss is not None:
                response = "CALC_DONE_PATHGAIN:" + str(pathloss)
                udp_socket.sendto(response.encode(), address)

        if message.startswith("CALC_REQUEST_DELAY:"):
            delay = manage_delay_request(message, sionna_structure)
            if delay is not None:
                response = "CALC_DONE_DELAY:" + str(delay)
                udp_socket.sendto(response.encode(), address)

        if message.startswith("CALC_REQUEST_LOS:"):
            los = manage_los_request(message, sionna_structure)
            if los is not None:
                response = "CALC_DONE_LOS:" + str(los)
                udp_socket.sendto(response.encode(), address)

        if message.startswith("SHUTDOWN_SIONNA"):
            print("Got SHUTDOWN_SIONNA message. Bye!")
            udp_socket.close()
            break


# Entry point
if __name__ == "__main__":
    main()
