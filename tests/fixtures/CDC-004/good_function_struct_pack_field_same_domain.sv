// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-003
// expect-none: CDC-004
// A straight-line function packs two arguments into the fields of a struct. The clka register captures only the field built
// from the clka argument; the clkb argument reaches the other field and an unused local, never this one.
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
  always_ff @(posedge clka) cap <= packed_w.a;
endmodule
