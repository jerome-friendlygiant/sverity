// sverity-args: --top top
// A constant select of a parameter that includes its X bit selects an X value.
// expect: LNT-039 WARNING 6
module top(output logic [1:0] y);
  localparam logic [3:0] P = 4'b10x1;
  assign y = P[1:0];
endmodule
