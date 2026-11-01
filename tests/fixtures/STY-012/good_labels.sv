// sverity-args: --top top
// expect-none: STY-012
module top(input logic a, output logic y);
  always_comb begin : blk
    y = a;
  end : blk
endmodule : top
