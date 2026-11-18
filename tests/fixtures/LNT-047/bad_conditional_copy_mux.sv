// sverity-args: --top top
// expect: LNT-047 WARNING 7
// revert: if (en) g = a; else g = b; => g = a;
// revert-expect-none: LNT-047
module top(input logic a,b,en,d, output logic q);
logic g;
always_comb if (en) g = a; else g = b;
always @(posedge g) q <= d;
endmodule
