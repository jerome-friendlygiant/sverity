module top(input logic clk,
  narrow_reset_n,
  output logic ready);

  (* async_reg = "true" *) logic first_n, local_n;
  // Upstream pulse generation and minimum duration are not supplied.
  always_ff @(posedge clk or negedge narrow_reset_n) begin
    if (!narrow_reset_n) begin first_n <= 0; local_n <= 0; end
    else begin first_n <= 1; local_n <= first_n; end
  end
  always_ff @(posedge clk or negedge local_n) begin
    if (!local_n) ready <= 0;
    else ready <= 1;
  end

endmodule
