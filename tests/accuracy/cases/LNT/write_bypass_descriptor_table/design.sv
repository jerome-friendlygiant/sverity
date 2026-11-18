// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Entries = 16,
  parameter int unsigned DescW   = 24,
  localparam int unsigned Aw     = $clog2(Entries)
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             push_i,
  input  logic [Aw-1:0]    push_idx_i,
  input  logic [DescW-1:0] push_desc_i,
  input  logic             fetch_i,
  input  logic [Aw-1:0]    fetch_idx_i,
  output logic [DescW-1:0] fetch_desc_o,
  output logic             fetch_valid_o
);

  logic [DescW-1:0] table_q [Entries];
  logic [Entries-1:0] present_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      present_q <= '0;
    end else if (push_i) begin
      present_q[push_idx_i] <= 1'b1;
    end
  end

  always_ff @(posedge clk_i) begin
    if (push_i) begin
      table_q[push_idx_i] <= push_desc_i;
    end
  end

  logic hit_push;
  assign hit_push = push_i & (push_idx_i == fetch_idx_i);

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      fetch_desc_o  <= '0;
      fetch_valid_o <= 1'b0;
    end else begin
      fetch_valid_o <= fetch_i & (present_q[fetch_idx_i] | hit_push);
      if (fetch_i) begin
        fetch_desc_o <= hit_push ? push_desc_i : table_q[fetch_idx_i];
      end
    end
  end

endmodule
