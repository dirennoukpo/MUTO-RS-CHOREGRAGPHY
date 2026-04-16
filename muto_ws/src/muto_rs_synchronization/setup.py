from setuptools import setup, find_packages

package_name = "muto_rs_synchronization"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/launch", [
            "launch/dance_choreography.launch.py",
        ]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Edwin",
    maintainer_email="edwin@example.com",
    description="MUTO-RS multi-robot synchronized dance choreography orchestration via ROS 2.",
    license="MIT",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "dance_leader.py=muto_rs_synchronization.dance_leader:main",
            "dance_follower.py=muto_rs_synchronization.dance_follower:main",
        ],
    },
)
