// sverity-args: --top top
// Every edge control is tested (LNT-069); the reset test is still checked: posedge rst runs the reset arm when rst is low.
// expect: LNT-058 ERROR 5
module top(input logic set, rst, output logic q);
  always_ff @(posedge set or posedge rst)
    if (!rst) q <= 1'b0;
    else if (set) q <= 1'b1;
endmodule
