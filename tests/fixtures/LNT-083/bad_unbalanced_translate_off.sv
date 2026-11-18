// sverity-args: --top top
// The region is never closed: a tool reads the rest of the file as switched off.
// expect: LNT-083 WARNING 6
module top(input logic a, output logic y);
  assign y = a;
  // synopsys translate_off
  initial $display("sim only");
endmodule
