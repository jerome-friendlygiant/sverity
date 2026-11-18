// sverity-args: --top top
// Each instance drives its own bit of n through an inout port connected to a constant select: no bit has two drivers.
// expect-none: LNT-009
module drv(inout wire p, input logic o);
  assign p = o;
endmodule
module top(input logic [1:0] o, output wire [1:0] y);
  wire [1:0] n;
  for (genvar g = 0; g < 2; g++) begin : G
    drv u(.p(n[g]), .o(o[g]));
  end
  assign y = n;
endmodule
