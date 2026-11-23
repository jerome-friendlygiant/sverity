// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-004 ERROR
// The same straight-line packing function, but the captured field is the one built from the clkb argument.
module top(input logic clka, clkb, input logic [3:0] ain, bin, output logic [3:0] cap);
  typedef struct packed {
    logic [3:0] a;
    logic [3:0] b;
  } pair_t;
  function automatic pair_t pack(input logic [3:0] left, input logic [3:0] right);
    pair_t r;
    logic unused;
    unused = 1'b0;
    unused ^= ^right;
    r.a = left;
    r.b = right;
    return r;
  endfunction
  logic [3:0] a_q, b_q;
  pair_t packed_w;
  always_ff @(posedge clka) a_q <= ain;
  always_ff @(posedge clkb) b_q <= bin;
  assign packed_w = pack(a_q, b_q);
  always_ff @(posedge clka) cap <= packed_w.b;
endmodule
