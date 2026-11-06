module reset_release (
  input logic clk,
  input logic source_n,
  output logic ready_n
);
  logic first_n;
  always_ff @(posedge clk or negedge source_n) begin
    if (!source_n) begin
      first_n <= 1'b0;
      ready_n <= 1'b0;
    end else begin
      first_n <= 1'b1;
      ready_n <= first_n;
    end
  end
endmodule

module top (
  input logic clk,
  input logic por_n,
  input logic tester_reset_n,
  input logic test_mode,
  input logic enable,
  output logic [7:0] cycles
);
  logic normal_n, test_n;
  wire selected_n;
  reset_release normal_i (clk, por_n, normal_n);
  reset_release tester_i (clk, tester_reset_n, test_n);
  // The manufacturing controller selects the reset source.
  assign selected_n = test_mode ? test_n : normal_n;
  always_ff @(posedge clk or negedge selected_n) begin
    if (!selected_n)
      cycles <= '0;
    else if (enable)
      cycles <= cycles + 1'b1;
  end
endmodule
