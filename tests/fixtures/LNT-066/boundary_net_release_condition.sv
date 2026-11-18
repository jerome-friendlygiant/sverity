// sverity-args: --top top
// expect-none: LNT-066
module top(input logic a, output logic y);
 wire released; assign released=1'bz;
 assign y = released ? a : 1'b0;
endmodule
