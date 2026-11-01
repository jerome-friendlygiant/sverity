// sverity-args: --top top
// The W=3 body is wrong (6 bits into 4); the W=2 body matches. Each body is counted once.
// expect-count: LNT-056 WARNING 1
module child #(parameter W = 2) (output logic [3:0] w);
  assign w = {W{2'b10}};
endmodule
module top(output logic [3:0] a, b);
  child u0(.w(a));
  child #(.W(3)) u1(.w(b));
endmodule
