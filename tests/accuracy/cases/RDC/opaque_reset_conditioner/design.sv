(* black_box *) module reset_cell(input wire clk,
  por_n,
  output wire local_n);


endmodule
module top(input wire clk,
  por_n,
  output logic ready);

  wire local_n;
  reset_cell u_reset(clk, por_n, local_n);
  always_ff @(posedge clk or negedge local_n) begin
    if (!local_n) ready <= 0;
    else ready <= 1;
  end

endmodule
