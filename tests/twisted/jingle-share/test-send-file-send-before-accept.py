from file_transfer_helper import SendFileTest, FileTransferTest, \
    ReceiveFileTest, exec_file_transfer_test


class SendFileBeforeAccept(SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.create_ft_channel,
                         self.provide_file, self.set_open, self.send_file, None,

                         self.wait_for_completion, None,

                         self.close_channel, self.done]

    def set_open(self):
        self.open = True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileBeforeAccept, ReceiveFileTest)
