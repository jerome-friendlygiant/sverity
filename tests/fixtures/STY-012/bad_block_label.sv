// sverity-args: --top top
// expect: STY-012 INFO 6
module top(input logic a, output logic y);
  always_comb begin : blk
    y = a;
  end
endmodule : top
