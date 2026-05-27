#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray
import math
import time

class MutoWaveController(Node):
    def __init__(self):
        super().__init__('muto_wave_controller')
        
        # 1. Publier directement vers le JointGroupPositionController de Gazebo
        self.cmd_pub = self.create_publisher(
            Float64MultiArray, 
            '/leg_controller/commands', 
            10
        )
        
        # 2. Fréquence d'envoi calée sur vos contrôleurs (50 Hz = toutes les 20ms)
        self.timer = self.create_timer(0.02, self.timer_callback)
        self.start_time = time.time()
        
        self.get_logger().info("=== Contrôleur de vague Muto RS initialisé ===")
        self.get_logger().info("Envoi des commandes de position à 50Hz dans Gazebo...")

    def timer_callback(self):
        # Temps écoulé en secondes
        t = time.time() - self.start_time
        
        # --- PARAMÈTRES DU MOUVEMENT ---
        frequency = 0.6  # Vitesse de l'oscillation (en Hz)
        amplitude_femur = 0.3  # Amplitude pour lever/baisser la patte (en radians)
        amplitude_tibia = 0.2  # Amplitude pour étendre la patte (en radians)
        
        # Calcul des angles dynamiques avec la fonction sinus
        wave_f = amplitude_femur * math.sin(2 * math.pi * frequency * t)
        wave_t = amplitude_tibia * math.cos(2 * math.pi * frequency * t)
        
        # Déphasage pour alterner les mouvements gauche/droite et avant/arrière
        wave_f_inv = -wave_f
        wave_t_inv = -wave_t

        # 3. Construction du tableau des 18 joints (Ordre strict de votre controllers.yaml)
        msg = Float64MultiArray()
        msg.data = [
            # --- CÔTÉ DROIT (RIGHT) ---
            0.0, wave_f, wave_t,          # zq (Front Right) : [Coxa, Femur, Tibia]
            0.0, wave_f_inv, wave_t_inv,  # zz (Mid Right)   : [Coxa, Femur, Tibia]
            0.0, wave_f, wave_t,          # zh (Rear Right)  : [Coxa, Femur, Tibia]

            # --- CÔTÉ GAUCHE (LEFT) ---
            0.0, wave_f_inv, wave_t_inv,  # yq (Front Left)  : [Coxa, Femur, Tibia]
            0.0, wave_f, wave_t,          # yz (Mid Left)    : [Coxa, Femur, Tibia]
            0.0, wave_f_inv, wave_t_inv   # yh (Rear Left)   : [Coxa, Femur, Tibia]
        ]
        
        # 4. Publication immédiate de la posture globale
        self.cmd_pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = MutoWaveController()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        node.get_logger().info("Arrêt du contrôleur demandé par l'utilisateur.")
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
