from robot_ctrl import RobotController

bot = RobotController("192.168.1.42")
bot.stand()
bot.forward(2.0)   # walks for 2 seconds then stops
bot.left(0.5)
bot.spin("right", rotations=0.5)
bot.sit()
bot.close()
