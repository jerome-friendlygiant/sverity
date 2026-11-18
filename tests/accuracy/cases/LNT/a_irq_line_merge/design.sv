// Interrupt source: sticky event latch cleared by software.
module irq_src (
  input  logic clk,
  input  logic rst_n,
  input  logic event_i,
  input  logic clear,
  output wire  irq_o
);
  logic pend_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      pend_q <= 1'b0;
    else if (clear)  pend_q <= 1'b0;
    else if (event_i) pend_q <= 1'b1;
  end

  assign irq_o = pend_q;
endmodule

module top (
  input  logic clk,
  input  logic rst_n,
  input  logic ev_a,
  input  logic ev_b,
  input  logic clear,
  input  logic enable,
  output logic irq
);
  wire irq_w;

  irq_src u_src (
    .clk     (clk),
    .rst_n   (rst_n),
    .event_i (ev_a),
    .clear   (clear),
    .irq_o   (irq_w)
  );

  assign irq_w = ev_b;
  assign irq   = irq_w & enable;
endmodule
