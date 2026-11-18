// Occupancy tracker for a small synchronous FIFO (flags only).
module top #(
  parameter int DEPTH = 8,
  parameter int AW    = $clog2(DEPTH)
) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        push,
  input  logic        pop,
  output logic        full,
  output logic        empty,
  output logic        almost_full,
  output logic [AW:0] level
);
  logic [AW:0] count_q;

  logic do_push;
  logic do_pop;
  assign do_push = push && !full;
  assign do_pop  = pop && !empty;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                  count_q <= '0;
    else if (do_push && !do_pop) count_q <= count_q + (AW+1)'(1);
    else if (do_pop && !do_push) count_q <= count_q - (AW+1)'(1);
  end

  assign level       = count_q;
  assign full        = (count_q == (AW+1)'(DEPTH));
  assign empty       = (count_q == '0);
  assign almost_full = (cnt_q >= (AW+1)'(DEPTH - 2));
endmodule
