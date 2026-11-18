// sverity-args: --top top
// A module writes the two elements of an interface array port, one by a constant index and one in a generate loop; two instances on two arrays give each element one writer.
// expect-none: LNT-009
interface bus_if; wire w; endinterface
module user(bus_if a[2], input logic x, y);
  assign a[0].w = x;
  for (genvar g = 1; g < 2; g++) begin : G assign a[g].w = y; end
endmodule
module top(input logic [3:0] o, output wire [3:0] z);
  bus_if bA[2](); bus_if bB[2](); user u1(.a(bA), .x(o[0]), .y(o[1])); user u2(.a(bB), .x(o[2]), .y(o[3]));
  assign z = {bA[0].w, bA[1].w, bB[0].w, bB[1].w};
endmodule
