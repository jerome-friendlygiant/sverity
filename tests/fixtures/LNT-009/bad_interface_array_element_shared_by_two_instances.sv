// sverity-args: --top top
// revert: user u2(.a(bA), => user u2(.a(bB),
// revert-expect-none: LNT-009
// Two instances write element 1 of one interface array port; element 0 has no writer.
// expect: LNT-009 ERROR 7
interface bus_if; wire w; endinterface
module user(bus_if a[2], input logic x); assign a[1].w = x; endmodule
module top(input logic [1:0] o, output wire [3:0] z);
  bus_if bA[2](); bus_if bB[2](); user u1(.a(bA), .x(o[0]));
  user u2(.a(bA), .x(o[1]));
  assign z = {bA[0].w, bA[1].w, bB[0].w, bB[1].w};
endmodule
