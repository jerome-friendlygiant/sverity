(* black_box *) module checksum_macro(input wire [31:0] data,
  output wire [7:0] check);


endmodule
module top(input wire [31:0] data,
  output wire [7:0] check);

  // The licensed macro implementation is deliberately absent.
  checksum_macro u_checksum(.data(data), .check(check));
  // Port widths alone do not define the checksum function.

endmodule
