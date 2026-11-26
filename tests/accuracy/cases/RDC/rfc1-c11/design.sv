// Transfer status block that reports whether the last transfer has finished.
module top (
  input  logic clk_a,
  input  logic rst_a_n,
  input  logic clk_b,
  input  logic rst_b_n,
  input  logic start_i,
  input  logic finish_b_i,
  output logic done_a_o
);
  logic fin_b_q, fin1_q, fin_q, run_q;

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) fin_b_q <= 1'b0;
    else          fin_b_q <= finish_b_i;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      fin1_q <= 1'b0;
      fin_q  <= 1'b0;
    end else begin
      fin1_q <= fin_b_q;
      fin_q  <= fin1_q;
    end

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) run_q <= 1'b0;
    else if (start_i) run_q <= 1'b1;
    else if (fin_q) run_q <= 1'b0;

  always_ff @(posedge clk_a or negedge rst_b_n)
    if (!rst_b_n) done_a_o <= 1'b0;
    else if (start_i) done_a_o <= 1'b0;
    else if (fin_q && run_q) done_a_o <= 1'b1;

endmodule
