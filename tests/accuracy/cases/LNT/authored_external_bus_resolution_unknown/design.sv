// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
(* black_box *) module pad_cell(input wire drive_i, enable_i, inout wire pad_io, output wire sense_o);
endmodule
module top(input wire drive_i, enable_i, inout wire pad_io, output wire sense_o);
  pad_cell u_pad(.drive_i(drive_i),.enable_i(enable_i),.pad_io(pad_io),.sense_o(sense_o));
endmodule
