// sverity-args: --top top
// a read between the two assignments and a self-referencing right side keep the first write live
// expect-none: LNT-053
module top(input logic a, b, output logic y, z);
  logic t, u;
  always_comb begin
    t = a;
    y = t;
    t = b;
    u = a;
    u = u + b;
    z = u;
  end
endmodule
