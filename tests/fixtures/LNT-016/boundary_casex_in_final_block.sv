// sverity-args: --top top
// The spec gives LNT-016 no INFO exemption: inside a final block the casex stays WARNING.
// expect: LNT-016 WARNING 6
module top(input logic [1:0] s);
  final begin
    casex (s)
      2'b1x: $display("one");
      default: $display("other");
    endcase
  end
endmodule
