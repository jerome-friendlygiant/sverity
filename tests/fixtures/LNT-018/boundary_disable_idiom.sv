// sverity-args: --top top
// a single literal 0 in an if is the intentional disable idiom: still reported, but as Info
// expect: LNT-018 INFO 7
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    if (1'b0) y = ~a;
  end
endmodule
