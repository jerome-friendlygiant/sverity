// sverity-args: --top top
// expect: LNT-069 WARNING 4
module top(input logic set, rst, output logic q);
  always_ff @(posedge set or posedge rst)
    if (rst) q <= 1'b0;
    else if (set) q <= 1'b1;
endmodule
