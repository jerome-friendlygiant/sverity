// sverity-args: --top top
// y reads x before the block assigns x: the read sees a stale value
// expect: LNT-044 WARNING 7
module top(input logic a, output logic y, z);
  logic x;
  always_comb begin
    y = x;
    x = a;
    z = x;
  end
endmodule
