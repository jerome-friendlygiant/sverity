// sverity-args: --top top
// A region that holds only simulation code (a message in an initial block): nothing synthesizable is switched off.
// expect-none: LNT-083
module top(input logic a, output logic y);
  assign y = a;
  // synopsys translate_off
  initial $display("sim only");
  // synopsys translate_on
endmodule
