// Adapted from common_cells src/cc_lossy_valid_to_stream.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright (C) 2023 ETH Zurich, University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

module top #(
  parameter int unsigned DataWidth = 32
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic                 clr_i,
  input  logic                 valid_i,
  input  logic [DataWidth-1:0] data_i,
  output logic                 valid_o,
  input  logic                 ready_i,
  output logic [DataWidth-1:0] data_o,
  output logic                 busy_o
);

  logic read_ptr_d, read_ptr_q;
  logic write_ptr_d, write_ptr_q;
  logic [1:0] pending_tx_counter_d, pending_tx_counter_q;
  logic [1:0][DataWidth-1:0] mem_d, mem_q;

  assign valid_o = pending_tx_counter_q != 0 || valid_i;

  always_comb begin : write_logic
    write_ptr_d = write_ptr_q;
    mem_d       = mem_q;
    if (valid_i) begin
      if (pending_tx_counter_q != 0 || !ready_i) begin
        if (pending_tx_counter_q == 2 && !ready_i) begin
          mem_d[write_ptr_q - 1'b1] = data_i;
        end else begin
          mem_d[write_ptr_q] = data_i;
          write_ptr_d = write_ptr_q + 1'b1;
        end
      end
    end
  end

  always_comb begin : read_logic
    read_ptr_d = read_ptr_q;
    data_o = mem_q[read_ptr_q];
    if (pending_tx_counter_q == 0 && valid_i) begin
      data_o = data_i;
    end else if (valid_o && ready_i) begin
      read_ptr_d = read_ptr_q + 1'b1;
    end
  end

  always_comb begin : count_transactions
    pending_tx_counter_d = pending_tx_counter_q;
    if (valid_i && valid_o && ready_i) begin
      pending_tx_counter_d = pending_tx_counter_q;
    end else if (valid_i && !(valid_o && ready_i)) begin
      if (pending_tx_counter_q != 2) begin
        pending_tx_counter_d = pending_tx_counter_q + 1'b1;
      end
    end else if (!valid_i && (valid_o && ready_i)) begin
      pending_tx_counter_d = pending_tx_counter_q - 1'b1;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      read_ptr_q           <= '0;
      write_ptr_q          <= '0;
      pending_tx_counter_q <= '0;
      mem_q                <= '0;
    end else if (clr_i) begin
      read_ptr_q           <= '0;
      write_ptr_q          <= '0;
      pending_tx_counter_q <= '0;
      mem_q                <= '0;
    end else begin
      read_ptr_q           <= read_ptr_d;
      write_ptr_q          <= write_ptr_d;
      pending_tx_counter_q <= pending_tx_counter_d;
      mem_q                <= mem_d;
    end
  end

  assign busy_o = pending_tx_counter_q != 0;

endmodule
