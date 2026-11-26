// Read-data relay that moves words from a producer-side memory into a consumer-domain register.
// Library cell: body not supplied.
(* black_box *) module sram_2p_hard (input logic clk_i, input logic rst_ni, input logic [5:0] addr_i, input logic en_i, output logic [31:0] q_o);
endmodule

module top (
  input  logic        clk_a,
  input  logic        rst_a_n,
  input  logic        clk_b,
  input  logic        rst_b_n,
  input  logic [5:0]  raddr_i,
  input  logic        rd_req_i,
  output logic [31:0] word_b_o,
  output logic        ok_b_o
);
  logic [31:0] rdata_a;
  logic        ok_a_q, ok_b1_q;

  sram_2p_hard u_mem (
    .clk_i  (clk_a),
    .rst_ni (rst_a_n),
    .addr_i (raddr_i),
    .en_i   (rd_req_i),
    .q_o    (rdata_a)
  );

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) ok_a_q <= 1'b0;
    else          ok_a_q <= rd_req_i;

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      ok_b1_q <= 1'b0;
      ok_b_o  <= 1'b0;
    end else begin
      ok_b1_q <= ok_a_q;
      ok_b_o  <= ok_b1_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) word_b_o <= '0;
    else if (ok_b1_q) word_b_o <= rdata_a;

endmodule
