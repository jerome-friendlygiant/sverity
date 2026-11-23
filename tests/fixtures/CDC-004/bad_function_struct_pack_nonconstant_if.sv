// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-004 ERROR
// The function branches on a non-constant condition, so it is outside the straight-line subset and keeps the coarse call model:
// the field the clka register captures can take the clkb argument.
module top(input logic clka, clkb, sel, input logic [3:0] ain, bin, output logic [3:0] cap);
  typedef struct packed {
    logic [3:0] a;
    logic [3:0] b;
  } pair_t;
  function automatic pair_t pack(input logic choose, input logic [3:0] left, input logic [3:0] right);
    pair_t r;
    r.a = left;
    r.b = right;
    if (choose) r.a = right;
    return r;
  endfunction
  logic [3:0] a_q, b_q;
  pair_t packed_w;
  always_ff @(posedge clka) a_q <= ain;
  always_ff @(posedge clkb) b_q <= bin;
  assign packed_w = pack(sel, a_q, b_q);
  always_ff @(posedge clka) cap <= packed_w.a;
endmodule
